#ifndef Network_H_
#define Network_H_  

#include <WiFi.h>
#include <Firebase_ESP_Client.h>

class Network
{
    private:
        String ssid;
        String pass;
        String ip;
        String gateway;
        FirebaseData fbdo;
        FirebaseAuth auth;
        FirebaseConfig config;
        unsigned long lastUpdateTime = 0;
        const unsigned long UPDATE_INTERVAL = 6000; // 6 seconds - avoid SSL memory leak
        const char* PARAM_INPUT_1 = "ssid";
        const char* PARAM_INPUT_2 = "pass";
        const char* PARAM_INPUT_3 = "ip";
        const char* PARAM_INPUT_4 = "gateway";
        const char* wifiConfigDir = "/wifi";
        const char* ssidPath = "/wifi/ssid.txt";
        const char* passPath = "/wifi/pass.txt";
        const char* ipPath = "/wifi/ip.txt";
        const char* gatewayPath = "/wifi/gateway.txt";
        IPAddress localIP;
        IPAddress localGateway;
        IPAddress subnet = IPAddress(255, 255, 0, 0);
        static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;

        bool initConfigStorage();
        String readConfigFile(const char* path);
        bool writeConfigFile(const char* path, const char* message);
        String readTextFile(const char* path);
        bool connectToSavedWiFi();
        void startConfigPortal();

    public:
        Network();
        bool initWiFi();
        void firebaseInit();
        bool firebaseReady();
        void firestoreDataUpdate(double temp, double humidity, int gasValue, int fireValue);
};

#endif
