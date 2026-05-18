#ifndef Network_H_
#define Network_H_  

#include <WiFi.h>
#include <Firebase_ESP_Client.h>

class Network
{
    private:
        FirebaseData fbdo;
        FirebaseAuth auth;
        FirebaseConfig config;
        unsigned long lastUpdateTime = 0;
        const unsigned long UPDATE_INTERVAL = 6000; // 6 seconds - avoid SSL memory leak


    public:
        Network();
        void initWiFi();
        void firebaseInit();
        void firestoreDataUpdate(double temp, double humidity, int gasValue);
};

#endif