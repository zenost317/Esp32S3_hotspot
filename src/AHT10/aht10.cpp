// #define ENABLE_USER_AUTH
// #define ENABLE_DATABASE

#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>

// Network and Firebase credentials
// #define WIFI_SSID "LAB 302-5GHz"
// #define WIFI_PASSWORD "vity1234"

// #define Web_API_KEY "AIzaSyBEOYaG4x8giWsCYeuX0vvqx804f0LSVNs"
// #define DATABASE_URL "https://esp32-hotpost-default-rtdb.asia-southeast1.firebasedatabase.app/"
// #define USER_EMAIL "swatgamer317@gmail.com"
// #define USER_PASS "test123456"

const char* WIFI_SSID = "HUCE-CBVC";
const char* WIFI_PASSWORD = "12345678";

const char* FIREBASE_PROJECT_ID = "firealarm-8587f";
const char* FIREBASE_API_KEY = "AIzaSyDrmSoZA86dYSZ0eDKLdC_zzGQspTqLoI0";

const char* COLLECTION_NAME = "sensor_readings";

// User function
// void processData(AsyncResult &aResult);
void sendDataToFirestore(int sensorId, int value);

// Authentication
// UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);

// Firebase components
// FirebaseApp app;
// WiFiClientSecure ssl_client;
// using AsyncClient = AsyncClientClass;
// AsyncClient aClient(ssl_client);
// RealtimeDatabase Database;

// Timer variables for sending data every 10 seconds
// unsigned long lastSendTime = 0;
// const unsigned long sendInterval = 10000; // 10 seconds in milliseconds

// Variables to send to the database
// int intValue = 0;
// float floatValue = 0.01;
// String stringValue = "";

//oled screen
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels 

#define OLED_SDA 14
#define OLED_SCL 13

#define SENS_SDA 8
#define SENS_SCL 9

TwoWire I2Cone = TwoWire(0);
TwoWire I2Ctwo = TwoWire(1);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2Cone, -1);

Adafruit_AHTX0 aht;

Adafruit_Sensor *aht_humidity, *aht_temp;

#define MP2_Pin 10
#define Gas_Threshold 

void setup(void) {
  Serial.begin(115200);

  //oled screen
  I2Cone.begin(OLED_SDA, OLED_SCL, 100000); 
  I2Ctwo.begin(SENS_SDA, SENS_SCL, 100000);

  delay(500);

  //sensors
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  Serial.println("AHT10 test!");

  if (!aht.begin(&I2Ctwo, 0, 0x38)) {
    Serial.println("Failed");
    display.setCursor(0, 0);
    display.println("AHT10 Failed");
    display.display();
    while (1);
  }

  analogSetAttenuation(ADC_11db);

  // Hiển thị trạng thái kết nối WiFi lên màn hình
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting to Wi-Fi...");
  display.display();

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  
  display.println("WiFi Connected!");
  display.display();
  delay(1000);

  // Configure SSL client
  // ssl_client.setInsecure();
  // ssl_client.setHandshakeTimeout(5);
  
  // // Initialize Firebase
  // initializeApp(aClient, app, getAuth(user_auth), processData, "authTask");
  // app.getApp<RealtimeDatabase>(Database);
  // Database.url(DATABASE_URL);
}

void loop() {

  int gasValue = analogRead(MP2_Pin);

  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp); // populate temp and humidity objects with fresh data

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE); // Thiết lập màu chữ trắng

  // Dòng 1: Nhiệt độ
  display.setCursor(0, 0);
  display.print(F("Temp: "));
  display.print(temp.temperature);
  display.print(" ");
  display.setTextSize(1);
  display.cp437(true);
  display.write(167);
  display.setTextSize(1);
  display.println("C");

  // Dòng 2: Độ ẩm
  display.setCursor(0, 10);
  display.print(F("Humidity: "));
  display.print(humidity.relative_humidity);
  display.println(" % rH");

  // Dòng 3: Gas
  display.setCursor(0, 20);
  display.print(F("Gas: "));
  display.println(gasValue);

  display.display();

  Serial.print("\t\tMP-2 Sensor Value: ");
  Serial.println(gasValue);
  Serial.print("\t\tHumidity: ");
  Serial.print(humidity.relative_humidity);
  Serial.println(" % rH");
  Serial.print("\t\tTemperature: ");
  Serial.print(temp.temperature);
  Serial.println(" °C");

  // app.loop();
  // // Check if authentication is ready
  // if (app.ready()){ 
  //   // Periodic data sending every 10 seconds
  //   unsigned long currentTime = millis();
  //   if (currentTime - lastSendTime >= sendInterval){
  //     // Update the last send time
  //     lastSendTime = currentTime;
      
  //     // Gửi giá trị cảm biến thực tế lên Firebase
  //     Database.set<float>(aClient, "/test/temperature", temp.temperature, processData, "RTDB_Send_Temp");
  //     Database.set<float>(aClient, "/test/humidity", humidity.relative_humidity, processData, "RTDB_Send_Humidity");
  //     Database.set<int>(aClient, "/test/gas", gasValue, processData, "RTDB_Send_Gas");
  //   }
  // }

  sendDataToFirestore(1, gasValue);
  sendDataToFirestore(2, humidity.relative_humidity);
  sendDataToFirestore(3, temp.temperature);

  delay(1000);
}

// void processData(AsyncResult &aResult) {
//   if (!aResult.isResult())
//     return;

//   if (aResult.isEvent())
//     Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());

//   if (aResult.isDebug())
//     Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());

//   if (aResult.isError())
//     Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

//   if (aResult.available())
//     Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
// }

void sendDataToFirestore(int sensorId, int value) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Error: Not connected to Wi-Fi.");
    return;
  }

  // Assemble the Firestore REST API URL.
  // By POSTing to the collection name, Firestore creates a random document ID.
  String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
               "/databases/(default)/documents/" + String(COLLECTION_NAME) +
               "?key=" + String(FIREBASE_API_KEY);

  // Create the request payload (body) in the JSON format required by Firestore.
  // This structure is the most critical part and a common source of errors.
  StaticJsonDocument<256> jsonDoc;
  
  JsonObject fields = jsonDoc.createNestedObject("fields");

  JsonObject sensorIdField = fields.createNestedObject("sensorId");
  sensorIdField["integerValue"] = sensorId;

  JsonObject valueField = fields.createNestedObject("value");
  valueField["integerValue"] = value;

  String jsonPayload;
  serializeJson(jsonDoc, jsonPayload);

  // Start the HTTP request
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  Serial.println("--- STARTING HTTP REQUEST ---");
  Serial.println("URL: " + url);
  Serial.println("Payload: " + jsonPayload);

  // Send the POST request with the JSON payload
  int httpResponseCode = http.POST(jsonPayload);

  // Check the server's response
  if (httpResponseCode > 0) {
    String responsePayload = http.getString();
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    Serial.print("Response: ");
    Serial.println(responsePayload);
  } else {
    Serial.print("Error on POST request. Error code: ");
    Serial.println(httpResponseCode);
  }

  Serial.println("--- END OF HTTP REQUEST --- \n");

  // Free resources
  http.end();
}