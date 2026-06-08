// #include <Wire.h>
// #include <HTTPClient.h>
// #include <ArduinoJson.h>
// #include <Adafruit_GFX.h>
// #include <Adafruit_SSD1306.h>
// #include <Adafruit_Sensor.h>
// #include <Adafruit_AHTX0.h>
// #include <WiFiClientSecure.h>

// #include "Network.h"

// void initNetwork();

// Network *network;

// //oled screen
// #define SCREEN_WIDTH 128 // OLED display width, in pixels
// #define SCREEN_HEIGHT 32 // OLED display height, in pixels 

// #define OLED_SDA 14
// #define OLED_SCL 13

// #define SENS_SDA 8
// #define SENS_SCL 9

// #define I2C_CLOCK_OLED 100000  // OLED can handle higher speed
// #define I2C_CLOCK_SENSOR 100000 // AHT10 needs lower speed

// TwoWire I2Cone = TwoWire(0);
// TwoWire I2Ctwo = TwoWire(1);

// Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2Cone, -1);

// Adafruit_AHTX0 aht;

// Adafruit_Sensor *aht_humidity, *aht_temp;

// #define MP2_Pin 10
// #define Gas_Threshold 500

// void setup(void) {
//   Serial.begin(115200);
//   Serial.setDebugOutput(false); // Disable I2C debug spam
//   delay(500);

//   //oled screen
//   I2Cone.begin(OLED_SDA, OLED_SCL, I2C_CLOCK_OLED); 
//   I2Ctwo.begin(SENS_SDA, SENS_SCL, I2C_CLOCK_SENSOR);

//   delay(500);

//   //sensors
//   if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
//     Serial.println(F("SSD1306 allocation failed"));
//     for(;;);
//   }
  
//   display.clearDisplay();
//   display.setTextColor(SSD1306_WHITE);

//   Serial.println("AHT10 test!");

//   // Retry AHT10 init multiple times
//   uint8_t aht_attempts = 0;
//   while (!aht.begin(&I2Ctwo, 0, 0x38) && aht_attempts < 5) {
//     Serial.println("AHT10 init failed, retrying...");
//     delay(500);
//     yield();
//     aht_attempts++;
//   }
  
//   if(aht_attempts >= 5) {
//     Serial.println("AHT10 Init Failed - Check wiring!");
//     display.setCursor(0, 0);
//     display.println("AHT10 Failed");
//     display.display();
//     // Don't hang, continue anyway
//   } else {
//     Serial.println("AHT10 OK");
//   }

//   analogSetAttenuation(ADC_11db);

//   // Hiển thị trạng thái kết nối WiFi lên màn hình
//   display.clearDisplay();
//   display.setCursor(0, 0);
//   display.println("Connecting to Wi-Fi...");
//   display.display();

//   // Connect to Wi-Fi
//   initNetwork();
  
//   // Chờ WiFi kết nối trước khi tiếp tục
//   uint8_t attempts = 0;
//   while(WiFi.status() != WL_CONNECTED && attempts < 20) {
//     delay(500);
//     attempts++;
//     yield(); // Reset watchdog
//   }
  
//   if(WiFi.status() == WL_CONNECTED) {
//     display.println("WiFi Connected!");
//     network->firebaseInit();
//   } else {
//     display.println("WiFi Failed!");
//   }
//   display.display();
  
//   // Delay với yield() để cho watchdog timer reset
//   for(int i = 0; i < 10; i++) {
//     delay(100);
//     yield();
//   }
// }

// void loop() {
//   yield(); // Allow watchdog timer to reset

//   int gasValue = analogRead(MP2_Pin);
//   yield();

//   // Read sensor with timeout protection
//   sensors_event_t humidity, temp;
//   bool sensor_ok = false;
  
//   // Try to read AHT10 with retry
//   for(int retry = 0; retry < 3; retry++) {
//     if(aht.getEvent(&humidity, &temp)) {
//       sensor_ok = true;
//       break;
//     }
//     yield();
//     delay(50);
//   }

//   display.clearDisplay();
//   display.setTextSize(1);
//   display.setTextColor(SSD1306_WHITE);

//   if(!sensor_ok) {
//     // Sensor error - display fallback
//     display.setCursor(0, 0);
//     display.println("Sensor Error!");
//     display.println("Gas:");
//     display.println(gasValue);
//   } else {
//     // Dòng 1: Nhiệt độ
//     display.setCursor(0, 0);
//     display.print(F("Temp: "));
//     display.print(temp.temperature);
//     display.print(" ");
//     display.setTextSize(1);
//     display.cp437(true);
//     display.write(167);
//     display.setTextSize(1);
//     display.println("C");

//     // Dòng 2: Độ ẩm
//     display.setCursor(0, 10);
//     display.print(F("Humidity: "));
//     display.print(humidity.relative_humidity);
//     display.println(" % rH");

//     // Dòng 3: Gas
//     display.setCursor(0, 20);
//     display.print(F("Gas: "));
//     display.println(gasValue);
//   }

//   display.display();
//   yield();

//   // Update Firebase only if sensor is OK
//   if(sensor_ok) {
//     network->firestoreDataUpdate(temp.temperature, humidity.relative_humidity, gasValue);
//   }

//   // Delay với yield() để cho watchdog timer reset
//   for(int i = 0; i < 10; i++) {
//     delay(100);
//     yield();
//   }
// }

// void initNetwork(){
//   network = new Network();
//   network->initWiFi();
// }