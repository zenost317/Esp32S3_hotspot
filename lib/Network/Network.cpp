#include "Network.h"
#include "addons/TokenHelper.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "FS.h"
#include "SD_MMC.h"

AsyncWebServer server(80);

#define FIREBASE_PROJECT_ID "firealarm-8587f"
#define FIREBASE_API_KEY "AIzaSyDrmSoZA86dYSZ0eDKLdC_zzGQspTqLoI0"

#define USER_EMAIL "swatgamer317@gmail.com"
#define USER_PASSWORD "test123456"

static const char* FIRESTORE_DOCUMENT_PATH = "devices/4845788";

Network::Network()
{
}

void WiFiEventConnected(WiFiEvent_t event, WiFiEventInfo_t info) 
{
    Serial.println("Wi-Fi connected");
}

void WiFiEventGotIP(WiFiEvent_t event, WiFiEventInfo_t info) 
{
    Serial.print("Wi-Fi got IP: ");
    Serial.println(WiFi.localIP());
}

void WiFiEventDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) 
{
    Serial.println("Wi-Fi disconnected");
    Serial.println(info.wifi_sta_disconnected.reason);
    Serial.println("Trying to reconnect");
    WiFi.reconnect();
}

void FirestoreTokenStatusCallback(TokenInfo info)
{
    Serial.printf("Token Info: type = %s, status = %s\n", getTokenType(info), getTokenStatus(info));
}

bool Network::initConfigStorage()
{
    if (SD_MMC.cardType() == CARD_NONE) {
        Serial.println("SD card is not mounted or no card is attached");
        return false;
    }

    if (!SD_MMC.exists(wifiConfigDir)) {
        Serial.printf("Config folder %s not found, creating it\r\n", wifiConfigDir);
        if (!SD_MMC.mkdir(wifiConfigDir)) {
            Serial.println("Failed to create /wifi folder on SD card");
            return false;
        }
    }

    Serial.println("SD card config storage ready");
    return true;
}

String Network::readConfigFile(const char* path)
{
    Serial.printf("Reading file: %s\r\n", path);

    File file = SD_MMC.open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        Serial.println("- failed to open file for reading");
        return String();
    }

    String fileContent;
    while (file.available()) {
        fileContent = file.readStringUntil('\n');
        break;
    }
    fileContent.trim();
    file.close();
    return fileContent;
}

bool Network::writeConfigFile(const char* path, const char* message)
{
    Serial.printf("Writing file: %s\r\n", path);

    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        Serial.println("- failed to open file for writing");
        return false;
    }

    bool success = false;
    if (file.print(message)) {
        Serial.println("- file written");
        success = true;
    } else {
        Serial.println("- write failed");
    }
    file.close();
    return success;
}

String Network::readTextFile(const char* path)
{
    File file = SD_MMC.open(path, FILE_READ);

    if (!file || file.isDirectory())
    {
        return "";
    }

    String content;

    while(file.available())
    {
        content += (char)file.read();
    }

    file.close();
    return content;
}

bool Network::connectToSavedWiFi()
{
    if (ssid == "") {
        Serial.println("[WiFi] No saved SSID. Starting AP.");
        return false;
    }

    // --- Bước 1: Scan để kiểm tra SSID có trong vùng phủ sóng không ---
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);   // đảm bảo không còn kết nối STA cũ
    delay(100);

    Serial.printf("[WiFi] Scanning for saved SSID: \"%s\" ...\n", ssid.c_str());
    int numNetworks = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);

    if (numNetworks == WIFI_SCAN_FAILED || numNetworks < 0) {
        Serial.println("[WiFi] Scan failed. Starting AP.");
        WiFi.scanDelete();
        return false;
    }

    if (numNetworks == 0) {
        Serial.println("[WiFi] No networks found. Starting AP.");
        WiFi.scanDelete();
        return false;
    }

    bool ssidFound = false;
    int bestRSSI = -1000;
    Serial.printf("[WiFi] Found %d network(s):\n", numNetworks);
    for (int i = 0; i < numNetworks; i++) {
        String foundSSID = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        Serial.printf("  [%d] \"%s\" RSSI=%d\n", i, foundSSID.c_str(), rssi);
        if (foundSSID == ssid) {
            ssidFound = true;
            if (rssi > bestRSSI) bestRSSI = rssi;
        }
    }
    WiFi.scanDelete();

    if (!ssidFound) {
        Serial.printf("[WiFi] Saved SSID \"%s\" not found in scan. Starting AP.\n", ssid.c_str());
        return false;
    }

    Serial.printf("[WiFi] SSID \"%s\" found (best RSSI=%d). Connecting...\n", ssid.c_str(), bestRSSI);

    // --- Bước 2: Cấu hình IP ---
    bool useStaticIP = (ip != "" && gateway != "");

    if (useStaticIP) {
        // Có IP tĩnh → dùng WiFi.config() (phù hợp router nhà)
        if (!localIP.fromString(ip.c_str()) || !localGateway.fromString(gateway.c_str())) {
            Serial.println("[WiFi] Invalid static IP or gateway in config. Falling back to DHCP.");
            useStaticIP = false;
        } else {
            IPAddress dns1 = localGateway;
            IPAddress dns2(8, 8, 8, 8);
            if (!WiFi.config(localIP, localGateway, subnet, dns1, dns2)) {
                Serial.println("[WiFi] WiFi.config() failed. Falling back to DHCP.");
                useStaticIP = false;
            }
        }
    }

    if (!useStaticIP) {
        // DHCP — ESP32 tự xin IP (phù hợp hotspot di động, mạng lạ)
        // Gọi WiFi.config với tất cả 0 để reset về DHCP
        WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(0,0,0,0));
        Serial.println("[WiFi] Using DHCP (no static IP configured).");
    } else {
        Serial.printf("[WiFi] Using static IP: %s / GW: %s\n", ip.c_str(), gateway.c_str());
    }

    // --- Bước 3: Kết nối ---
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long startedAt = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startedAt >= WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("\n[WiFi] Connection timeout. Starting AP.");
            WiFi.disconnect(true);   // tắt STA, giải phóng radio trước khi chuyển AP
            delay(100);
            return false;
        }
        delay(500);
        Serial.print(".");
        yield();
    }

    Serial.println();
    Serial.printf("[WiFi] Connected! IP: %s (via %s)\n",
                  WiFi.localIP().toString().c_str(),
                  useStaticIP ? "static" : "DHCP");
    WiFi.onEvent(WiFiEventConnected,    ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(WiFiEventGotIP,        ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(WiFiEventDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.setAutoReconnect(true);
    return true;
}

void Network::startConfigPortal()
{
    Serial.println("[AP] Starting config portal (Access Point mode)");
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP-WIFI-MANAGER", nullptr);

    IPAddress apIP = WiFi.softAPIP();
    Serial.print("[AP] IP address: ");
    Serial.println(apIP);

    server.on("/", HTTP_GET,
    [](AsyncWebServerRequest *request)
    {
        File file = SD_MMC.open("/wifimanager.html");

        if(!file)
        {
            request->send(404, "text/plain", "wifimanager.html not found");
            return;
        }

        request->send(SD_MMC, "/wifimanager.html", "text/html");
    });

    server.on("/", HTTP_POST, [this](AsyncWebServerRequest *request) {
        // --- Debug: in toàn bộ params nhận được ---
        int params = request->params();
        Serial.printf("[AP] POST received: %d param(s)\n", params);

        if (params == 0) {
            Serial.println("[AP] WARNING: No POST params received! Check form enctype.");
            request->send(400, "text/plain", "No parameters received. Check form encoding.");
            return;
        }

        for (int i = 0; i < params; i++) {
            const AsyncWebParameter* p = request->getParam(i);
            Serial.printf("[AP]  param[%d] name=\"%s\" value=\"%s\" isPost=%d\n",
                          i, p->name().c_str(), p->value().c_str(), p->isPost() ? 1 : 0);
        }

        // --- Kiểm tra SD card có sẵn không ---
        if (SD_MMC.cardType() == CARD_NONE) {
            Serial.println("[AP] ERROR: SD card not mounted. Cannot save WiFi config.");
            request->send(500, "text/plain", "SD card not available. Cannot save config.");
            return;
        }

        // --- Đọc và lưu từng param ---
        bool saved = true;
        for (int i = 0; i < params; i++) {
            const AsyncWebParameter* p = request->getParam(i);
            if (!p->isPost()) {
                continue;
            }

            if (p->name() == PARAM_INPUT_1) {
                ssid = p->value().c_str();
                Serial.printf("[AP] SSID     = \"%s\"\n", ssid.c_str());
                saved = writeConfigFile(ssidPath, ssid.c_str()) && saved;
            }

            if (p->name() == PARAM_INPUT_2) {
                pass = p->value().c_str();
                Serial.printf("[AP] Password = \"%s\" (%d chars)\n", pass.c_str(), pass.length());
                saved = writeConfigFile(passPath, pass.c_str()) && saved;
            }

            // IP và gateway là tùy chọn — để trống = dùng DHCP (hotspot di động)
            if (p->name() == PARAM_INPUT_3) {
                ip = p->value().c_str();
                ip.trim();
                Serial.printf("[AP] IP       = \"%s\"%s\n", ip.c_str(), ip.isEmpty() ? " (DHCP)" : "");
                saved = writeConfigFile(ipPath, ip.c_str()) && saved;
            }

            if (p->name() == PARAM_INPUT_4) {
                gateway = p->value().c_str();
                gateway.trim();
                Serial.printf("[AP] Gateway  = \"%s\"%s\n", gateway.c_str(), gateway.isEmpty() ? " (DHCP)" : "");
                saved = writeConfigFile(gatewayPath, gateway.c_str()) && saved;
            }
        }

        // SSID là trường bắt buộc
        if (ssid.isEmpty()) {
            request->send(400, "text/plain", "SSID is required.");
            return;
        }

        if (!saved) {
            Serial.println("[AP] ERROR: Failed to write one or more config files to SD card.");
            request->send(500, "text/plain", "Failed to save WiFi config. Check SD card.");
            return;
        }

        Serial.println("[AP] Config saved. Restarting in 3s...");
        String msg = "Done. ESP will restart and ";
        if (ip.isEmpty()) {
            msg += "use DHCP (IP assigned by hotspot).";
        } else {
            msg += "connect to: " + ip;
        }
        request->send(200, "text/plain", msg);
        delay(3000);
        ESP.restart();
    });

    server.on("/style.css", HTTP_GET,
    [](AsyncWebServerRequest *request)
    {
        File file = SD_MMC.open("/style.css");

        if(!file)
        {
            request->send(404, "text/plain", "style.css not found");
            return;
        }

        request->send(SD_MMC, "/style.css", "text/css");
    });

    server.begin();
    Serial.println("[AP] Web server started on port 80");
}

bool Network::initWiFi()
{
    bool storageReady = initConfigStorage();

    if (storageReady) {
        ssid     = readConfigFile(ssidPath);
        pass     = readConfigFile(passPath);
        ip       = readConfigFile(ipPath);
        gateway  = readConfigFile(gatewayPath);

        Serial.println("[WiFi] Loaded config from SD:");
        Serial.printf("  SSID    = \"%s\"\n", ssid.c_str());
        Serial.printf("  Pass    = \"%s\" (%d chars)\n", pass.c_str(), pass.length());
        Serial.printf("  IP      = \"%s\"\n", ip.c_str());
        Serial.printf("  Gateway = \"%s\"\n", gateway.c_str());
    } else {
        Serial.println("[WiFi] SD storage not ready. Cannot load saved credentials.");
    }

    if (connectToSavedWiFi()) {
        return true;  // kết nối thành công
    }

    // Không kết nối được → mở AP để cấu hình
    startConfigPortal();
    return false;
}

void Network::firebaseInit() 
{
    config.api_key = FIREBASE_API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    
    // Add connection timeout to prevent memory leak
    config.timeout.serverResponse = 10 * 1000; // 10 seconds
    config.token_status_callback = FirestoreTokenStatusCallback;

    Firebase.reconnectWiFi(true);
    Firebase.begin(&config, &auth);
    Serial.printf("[Firebase] Init requested for project %s, document %s\n", FIREBASE_PROJECT_ID, FIRESTORE_DOCUMENT_PATH);
}

bool Network::firebaseReady()
{
    return Firebase.ready();
}

// void Network::firestoreDataUpdate(double temp, double humidity, int gasValue, int fireValue)
// {
//     // Kiểm tra timeout để tránh gọi Firebase quá tần suất
//     unsigned long currentTime = millis();
//     if(currentTime - lastUpdateTime < UPDATE_INTERVAL)
//     {
//         yield();
//         return;
//     }
    
//     if(WiFi.status() != WL_CONNECTED)
//     {
//         static unsigned long lastWifiLogTime = 0;
//         if(currentTime - lastWifiLogTime >= 5000)
//         {
//             Serial.printf("[Firestore] Skip update: WiFi disconnected, status=%d\n", WiFi.status());
//             lastWifiLogTime = currentTime;
//         }
//         yield();
//         return;
//     }

//     if(!Firebase.ready())
//     {
//         static unsigned long lastFirebaseLogTime = 0;
//         if(currentTime - lastFirebaseLogTime >= 5000)
//         {
//             Serial.println("[Firestore] Skip update: Firebase is not ready yet");
//             lastFirebaseLogTime = currentTime;
//         }
//         yield();
//         return;
//     }

//     String documentPath = FIRESTORE_DOCUMENT_PATH;
//     FirebaseJson content;
    
//     content.set("fields/temperatureValue/doubleValue", temp);
//     content.set("fields/humidityValue/doubleValue", humidity);
//     content.set("fields/smokeValue/integerValue", String(gasValue));
//     content.set("fields/fireValue/integerValue", String(fireValue));

//     bool success = Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "temperatureValue,humidityValue,smokeValue,fireValue");
    
//     if(success)
//     {
//         Serial.printf("[Firestore] Update SUCCESS\n");
//         lastUpdateTime = currentTime;
//         yield();
//     }
//     else
//     {
//         String errorMsg = fbdo.errorReason().c_str();
//         Serial.printf("[Firestore] Error: %s\n", errorMsg.c_str());
//         yield();
//     }
    
//     // Force cleanup to avoid memory leak
//     fbdo.clear();
// }

bool Network::firestoreDataUpdate(
    double temp,
    double humidity,
    int gasValue,
    int fireValue,
    bool cameraFireDetected,
    bool cameraSmokeDetected,
    bool sensorOverThreshold,
    const String& alertType,
    const String& edgeCase,
    const String& imagePath,
    const String& logPath,
    const String& timestamp
)
{
    unsigned long currentTime = millis();
    if (currentTime - lastUpdateTime < UPDATE_INTERVAL) {
        yield();
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastWifiLogTime = 0;
        if (currentTime - lastWifiLogTime >= 5000) {
            Serial.printf("[Firestore] Skip: WiFi disconnected, status=%d\n", WiFi.status());
            lastWifiLogTime = currentTime;
        }
        yield();
        return false;
    }

    if (!Firebase.ready()) {
        static unsigned long lastFirebaseLogTime = 0;
        if (currentTime - lastFirebaseLogTime >= 5000) {
            Serial.println("[Firestore] Skip: Firebase not ready");
            lastFirebaseLogTime = currentTime;
        }
        yield();
        return false;
    }

    // --- Build payload ---
    FirebaseJson content;
    content.set("fields/temperatureValue/doubleValue", temp);
    content.set("fields/humidityValue/doubleValue",    humidity);
    content.set("fields/smokeValue/integerValue",      String(gasValue));
    content.set("fields/fireValue/integerValue",       String(fireValue));

    String updateMask = "temperatureValue,humidityValue,smokeValue,fireValue";

    // Nếu timestamp có giá trị → bổ sung AI fields
    bool hasAiData = !timestamp.isEmpty();
    if (hasAiData) {
        content.set("fields/cameraFireDetected/booleanValue",  cameraFireDetected);
        content.set("fields/cameraSmokeDetected/booleanValue", cameraSmokeDetected);
        content.set("fields/sensorOverThreshold/booleanValue", sensorOverThreshold);
        content.set("fields/cameraAlertType/stringValue",      alertType);
        content.set("fields/edgeCase/stringValue",             edgeCase);
        content.set("fields/lastAiImagePath/stringValue",      imagePath);
        content.set("fields/lastAiLogPath/stringValue",        logPath);
        content.set("fields/aiUpdatedAt/stringValue",          timestamp);

        updateMask += ",cameraFireDetected,cameraSmokeDetected,sensorOverThreshold"
                      ",cameraAlertType,edgeCase,lastAiImagePath,lastAiLogPath,aiUpdatedAt";
    }

    // --- Gửi lên Firestore ---
    bool success = Firebase.Firestore.patchDocument(
        &fbdo,
        FIREBASE_PROJECT_ID,
        "",
        FIRESTORE_DOCUMENT_PATH,
        content.raw(),
        updateMask.c_str()
    );

    if (success) {
        Serial.printf("[Firestore] Update OK%s\n", hasAiData ? " (+AI)" : "");
        lastUpdateTime = currentTime;
    } else {
        Serial.printf("[Firestore] Error: %s\n", fbdo.errorReason().c_str());
    }

    fbdo.clear();
    yield();
    return success;
}