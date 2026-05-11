#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in 

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

void testscrolltext(void) {
  display.clearDisplay();

  display.setTextSize(1); // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("scroll"));
  display.display();      // Show initial text
  delay(100);

  // Scroll in various directions, pausing in-between:
  // display.startscrollright(0x00, 0x0F);
  // delay(2000);
  // display.stopscroll();
  // delay(1000);
  // display.startscrollleft(0x00, 0x0F);
  // delay(2000);
  // display.stopscroll();
  // delay(1000);
  // display.startscrolldiagright(0x00, 0x07);
  // delay(2000);
  // display.startscrolldiagleft(0x00, 0x07);
  // delay(2000);
  // display.stopscroll();
  // delay(1000);
}

void setup(void) {
  Serial.begin(115200);

  I2Cone.begin(OLED_SDA, OLED_SCL, 100000); 
  I2Ctwo.begin(SENS_SDA, SENS_SCL, 100000);

  delay(500);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  Serial.println("AHT10 test!");

  if (!aht.begin(&I2Ctwo, 0, 0x38)) {
    Serial.println("Failed");
    while (1);
  }

  analogSetAttenuation(ADC_11db);
  
  display.clearDisplay();
  display.drawPixel(10, 10, SSD1306_WHITE);
  display.display();
  delay(2000);

  testscrolltext();
}

void loop() {

  int gasValue = analogRead(MP2_Pin);

  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp); // populate temp and humidity objects with fresh data

  display.clearDisplay();
  display.setTextSize(1);

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

  delay(1000);
  
}