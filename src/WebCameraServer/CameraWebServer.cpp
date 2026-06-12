#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "esp_http_server.h"
#include <Wire.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiClientSecure.h>
#include "FS.h"
#include "SD_MMC.h"

#include "Network.h"
#include "SensorHandler.h"
#include "board_config.h"

void initNetwork();

Network *network;

// Pin SD card trên ESP32-S3 WROOM N16R8 CAM
#define SD_MMC_CLK  39
#define SD_MMC_CMD  38
#define SD_MMC_D0   40

bool sdReady = false;

// oled screen
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels 

#define OLED_SDA 2
#define OLED_SCL 1

// sensors
#define SENS_SDA 48
#define SENS_SCL 47

float tempC = 0, humiPct = 0;

#define MP2_Pin 14

// I2C clock speeds
#define I2C_CLOCK_OLED 100000  // OLED can handle higher speed
#define I2C_CLOCK_SENSOR 100000 // AHT10 needs lower speed

TwoWire I2Cone = TwoWire(0);
TwoWire I2Ctwo = TwoWire(1);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2Cone, -1);

#define Gas_Threshold 500

// ================== SD CARD CONFIG ==================
void sdSetup() {
  // Bắt buộc setPins() TRƯỚC begin() trên ESP32-S3
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);

  // 1-bit mode (true) — đúng với hardware của board này
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD_MMC mount failed");
    return;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Serial.println("SD card mounted");
  sdReady = true;
}

void sdInfo() {
  if (!sdReady) return;
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %llu MB\n", cardSize);
}

void sdWriteTest() {
  if (!sdReady) return;
  File file = SD_MMC.open("/test.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  file.println("IoTLabs");
  file.println("Nghien cuu - Sang tao - Thu nghiem");
  file.println("Website: https://iotlabs.vn");
  file.close();
  Serial.println("Write OK");
}

void sdReadTest() {
  if (!sdReady) return;
  File file = SD_MMC.open("/test.txt");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

// ================== MJPEG STREAM CONFIG ==================
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char* STREAM_BOUNDARY = "--frame";
static const char* STREAM_PART =
    "Content-Type: image/jpeg\r\n"
    "Content-Length: %u\r\n\r\n";

static httpd_handle_t s_httpd = NULL;

// =============== Helpers ===============
static void logMemory(const char* tag) {
  uint32_t heap = ESP.getFreeHeap();
  uint32_t psram = ESP.getFreePsram();
  Serial.printf("[%s] freeHeap=%u bytes | freePSRAM=%u bytes\n", tag, heap, psram);
}

static bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming


  // Tối ưu ổn định trước
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count     = 2;

  // Nếu không có PSRAM, giảm cấu hình để tránh crash
  if (!psramFound()) {
    Serial.println("PSRAM not found -> downgrade camera settings");
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_count   = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  // Một số tối ưu nhỏ cho chất lượng/ổn định
  s->set_framesize(s, config.frame_size);
  s->set_quality(s, config.jpeg_quality);
  s->set_brightness(s, 0);
  s->set_contrast(s, 0);
  s->set_vflip(s, 1);

  Serial.println("Camera init OK");
  return true;
}

// =============== HTTP Handlers ===============
static esp_err_t index_handler(httpd_req_t* req) {
  // Redirect to stream
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/stream");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t stream_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      return ESP_FAIL;
    }

    // Boundary
    if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }
    if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    // Header part
    char part_buf[64];
    int hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
    if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    // JPEG data
    if (httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len) != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    // End of frame
    if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    esp_camera_fb_return(fb);

    // Giảm tải CPU/WiFi, giúp ổn định hơn khi router yếu
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // Kết thúc response
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 8;

  httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = index_handler,
      .user_ctx = NULL};

  httpd_uri_t stream_uri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL};

  if (httpd_start(&s_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(s_httpd, &index_uri);
    httpd_register_uri_handler(s_httpd, &stream_uri);
    Serial.println("HTTP server started");
  } else {
    Serial.println("HTTP server start failed");
  }
}


void setup() {
  Serial.begin(115200);

  Serial.setDebugOutput(false); // Disable I2C debug spam
  delay(500);

  //oled screen
  I2Cone.begin(OLED_SDA, OLED_SCL, I2C_CLOCK_OLED); 
  I2Ctwo.begin(SENS_SDA, SENS_SCL, I2C_CLOCK_SENSOR);

  delay(500);

  //sensors
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // SAU (thay vào)
  Serial.println("AHT10 test!");
  if (!sensorInit(I2Ctwo)) {
    Serial.println("AHT10 Init Failed - Check wiring!");
    display.setCursor(0, 0);
    display.println("AHT10 Failed");
    display.display();
  } else {
    Serial.println("AHT10 OK");
  }

  analogSetAttenuation(ADC_11db);

  // Hiển thị trạng thái kết nối WiFi lên màn hình
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting to Wi-Fi...");
  display.display();

  // PSRAM check
  if (psramFound()) Serial.println("PSRAM: FOUND");
  else Serial.println("PSRAM: NOT FOUND");

  // Connect to Wi-Fi
  initNetwork();

    // Camera
  if (!initCamera()) {
    Serial.println("Camera init failed -> stop");
    while (true) delay(1000);
  }

  // Server
  startCameraServer();

  // SD card
  sdSetup();
  sdInfo();
  sdWriteTest();
  sdReadTest();

  logMemory("BOOT");
}

void loop() {
  yield();

  int gasValue = analogRead(MP2_Pin);
  yield();

  float tempC = 0, humiPct = 0;
  bool sensor_ok = sensorRead(tempC, humiPct); // retry nằm trong SensorHandler

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (!sensor_ok) {
    display.setCursor(0, 0);
    display.println("Sensor Error!");
    display.println("Gas:");
    display.println(gasValue);
  } else {
    display.setCursor(0, 0);
    display.print(F("Temp: "));
    display.print(tempC);
    display.print(" ");
    display.cp437(true);
    display.write(167);  // ký tự °
    display.println("C");

    display.setCursor(0, 10);
    display.print(F("Humidity: "));
    display.print(humiPct);
    display.println(" % rH");

    display.setCursor(0, 20);
    display.print(F("Gas: "));
    display.println(gasValue);
  }

  display.display();
  yield();

  if (sensor_ok) {
    network->firestoreDataUpdate(tempC, humiPct, gasValue);
  }

  for (int i = 0; i < 10; i++) {
    delay(100);
    yield();
  }

  // In log nhe moi 5s de theo doi memory (tranh spam Serial)
  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    logMemory("RUN");
  }

  delay(10);
}

void initNetwork(){
  network = new Network();
  network->initWiFi();
}
