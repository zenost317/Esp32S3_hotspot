#include "Network.h"
#include "addons/TokenHelper.h"

#define WIFI_SSID "HIEU"
#define WIFI_PASSWORD "31072004"

#define FIREBASE_PROJECT_ID "firealarm-8587f"
#define FIREBASE_API_KEY "AIzaSyDrmSoZA86dYSZ0eDKLdC_zzGQspTqLoI0"

#define USER_EMAIL "swatgamer317@gmail.com"
#define USER_PASSWORD "test123456"

static Network *instance = NULL;

Network::Network()
{
    instance = this;
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
}

void FirestoreTokenStatusCallback(TokenInfo info)
{
    Serial.printf("Token Info: type = %s, status = %s\n", getTokenType(info), getTokenStatus(info));
}

void Network::initWiFi()
{
    WiFi.disconnect();
    WiFi.onEvent(WiFiEventConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
    WiFi.onEvent(WiFiEventGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(WiFiEventDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   
}

void Network::firebaseInit() 
{
    config.api_key = FIREBASE_API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    
    // Add connection timeout to prevent memory leak
    config.timeout.serverResponse = 10 * 1000; // 10 seconds
    config.token_status_callback = FirestoreTokenStatusCallback;

    Firebase.begin(&config, &auth);
}

void Network::firestoreDataUpdate(double temp, double humidity, int gasValue)
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
        yield();
        return;
    }

    String documentPath = "devices/4845788";
    FirebaseJson content;
    
    // Cập nhật giá trị của các fields root-level
    content.set("fields/temperatureValue/doubleValue", temp);
    content.set("fields/humidityValue/doubleValue", humidity);
    content.set("fields/smokeValue/integerValue", gasValue);

    // updateMask phải match với tên fields thực tế
    bool success = Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw(), "temperatureValue,humidityValue,smokeValue");
    
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