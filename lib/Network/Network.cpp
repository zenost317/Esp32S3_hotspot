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
    if (ssid == "" || ip == "" || gateway == "") {
        Serial.println("Undefined SSID, IP address, or gateway.");
        return false;
    }

    WiFi.mode(WIFI_STA);

    if (!localIP.fromString(ip.c_str()) || !localGateway.fromString(gateway.c_str())) {
        Serial.println("Invalid IP address or gateway.");
        return false;
    }

    IPAddress dns1 = localGateway;   // router thường tự forward DNS
    IPAddress dns2(8, 8, 8, 8);      // Google DNS dự phòng

    if (!WiFi.config(localIP, localGateway, subnet, dns1, dns2)) {
        Serial.println("STA Failed to configure");
        return false;
    }

    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.println("Connecting to WiFi...");

    unsigned long startedAt = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startedAt >= WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("Failed to connect.");
            return false;
        }
        delay(500);
        Serial.print(".");
        yield();
    }

    Serial.println();
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    WiFi.onEvent(WiFiEventConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(WiFiEventGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(WiFiEventDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.setAutoReconnect(true);
    return true;
}

void Network::startConfigPortal()
{
    Serial.println("Setting AP (Access Point)");
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP-WIFI-MANAGER", nullptr);

    IPAddress apIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
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
        bool saved = true;
        int params = request->params();
        for (int i = 0; i < params; i++) {
            const AsyncWebParameter* p = request->getParam(i);
            if (!p->isPost()) {
                continue;
            }

            if (p->name() == PARAM_INPUT_1) {
                ssid = p->value().c_str();
                Serial.print("SSID set to: ");
                Serial.println(ssid);
                saved = writeConfigFile(ssidPath, ssid.c_str()) && saved;
            }

            if (p->name() == PARAM_INPUT_2) {
                pass = p->value().c_str();
                Serial.print("Password set to: ");
                Serial.println(pass);
                saved = writeConfigFile(passPath, pass.c_str()) && saved;
            }

            if (p->name() == PARAM_INPUT_3) {
                ip = p->value().c_str();
                Serial.print("IP Address set to: ");
                Serial.println(ip);
                saved = writeConfigFile(ipPath, ip.c_str()) && saved;
            }

            if (p->name() == PARAM_INPUT_4) {
                gateway = p->value().c_str();
                Serial.print("Gateway set to: ");
                Serial.println(gateway);
                saved = writeConfigFile(gatewayPath, gateway.c_str()) && saved;
            }
        }

        if (!saved) {
            request->send(500, "text/plain", "Failed to save WiFi config to SD card. Please check SD card.");
            return;
        }

        request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
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
}

bool Network::initWiFi()
{
    bool storageReady = initConfigStorage();

    if (storageReady) {
        ssid = readConfigFile(ssidPath);
        pass = readConfigFile(passPath);
        ip = readConfigFile(ipPath);
        gateway = readConfigFile(gatewayPath);
    }

    Serial.println(ssid);
    Serial.println(pass);
    Serial.println(ip);
    Serial.println(gateway);

    if (connectToSavedWiFi()) {
        return true;
    }

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

void Network::firestoreDataUpdate(double temp, double humidity, int gasValue, int fireValue)
{
    // Kiểm tra timeout để tránh gọi Firebase quá tần suất
    unsigned long currentTime = millis();
    if(currentTime - lastUpdateTime < UPDATE_INTERVAL)
    {
        yield();
        return;
    }
    
    if(WiFi.status() != WL_CONNECTED)
    {
        static unsigned long lastWifiLogTime = 0;
        if(currentTime - lastWifiLogTime >= 5000)
        {
            Serial.printf("[Firestore] Skip update: WiFi disconnected, status=%d\n", WiFi.status());
            lastWifiLogTime = currentTime;
        }
        yield();
        return;
    }

    if(!Firebase.ready())
    {
        static unsigned long lastFirebaseLogTime = 0;
        if(currentTime - lastFirebaseLogTime >= 5000)
        {
            Serial.println("[Firestore] Skip update: Firebase is not ready yet");
            lastFirebaseLogTime = currentTime;
        }
        yield();
        return;
    }

    String documentPath = FIRESTORE_DOCUMENT_PATH;
    FirebaseJson content;
    
    content.set("fields/temperatureValue/doubleValue", temp);
    content.set("fields/humidityValue/doubleValue", humidity);
    content.set("fields/smokeValue/integerValue", String(gasValue));
    content.set("fields/fireValue/integerValue", String(fireValue));

    bool success = Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "temperatureValue,humidityValue,smokeValue,fireValue");
    
    if(success)
    {
        Serial.printf("[Firestore] Update SUCCESS\n");
        lastUpdateTime = currentTime;
        yield();
    }
    else
    {
        String errorMsg = fbdo.errorReason().c_str();
        Serial.printf("[Firestore] Error: %s\n", errorMsg.c_str());
        yield();
    }
    
    // Force cleanup to avoid memory leak
    fbdo.clear();
}

bool Network::firestoreAiDataUpdate(
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
    if(currentTime - lastAiUpdateTime < AI_UPDATE_INTERVAL)
    {
        yield();
        return false;
    }

    if(WiFi.status() != WL_CONNECTED)
    {
        Serial.printf("[Firestore AI] Skip update: WiFi disconnected, status=%d\n", WiFi.status());
        yield();
        return false;
    }

    if(!Firebase.ready())
    {
        Serial.println("[Firestore AI] Skip update: Firebase is not ready yet");
        yield();
        return false;
    }

    String documentPath = FIRESTORE_DOCUMENT_PATH;
    FirebaseJson content;

    content.set("fields/temperatureValue/doubleValue", temp);
    content.set("fields/humidityValue/doubleValue", humidity);
    content.set("fields/smokeValue/integerValue", String(gasValue));
    content.set("fields/fireValue/integerValue", String(fireValue));
    content.set("fields/cameraFireDetected/booleanValue", cameraFireDetected);
    content.set("fields/cameraSmokeDetected/booleanValue", cameraSmokeDetected);
    content.set("fields/sensorOverThreshold/booleanValue", sensorOverThreshold);
    content.set("fields/cameraAlertType/stringValue", alertType);
    content.set("fields/edgeCase/stringValue", edgeCase);
    content.set("fields/lastAiImagePath/stringValue", imagePath);
    content.set("fields/lastAiLogPath/stringValue", logPath);
    content.set("fields/aiUpdatedAt/stringValue", timestamp);

    const char* updateMask =
        "temperatureValue,humidityValue,smokeValue,fireValue,"
        "cameraFireDetected,cameraSmokeDetected,sensorOverThreshold,"
        "cameraAlertType,edgeCase,lastAiImagePath,lastAiLogPath,aiUpdatedAt";

    bool success = Firebase.Firestore.patchDocument(
        &fbdo,
        FIREBASE_PROJECT_ID,
        "",
        documentPath.c_str(),
        content.raw(),
        updateMask
    );

    if(success)
    {
        Serial.println("[Firestore AI] Update SUCCESS");
        lastAiUpdateTime = currentTime;
    }
    else
    {
        String errorMsg = fbdo.errorReason().c_str();
        Serial.printf("[Firestore AI] Error: %s\n", errorMsg.c_str());
    }

    fbdo.clear();
    yield();
    return success;
}
