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
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <time.h>

#include "Network.h"
#include "SensorHandler.h"
#include "board_config.h"

bool initNetwork();

Network *network;

// Pin SD card trên ESP32-S3 WROOM N16R8 CAM
#define SD_MMC_CLK  39
#define SD_MMC_CMD  38
#define SD_MMC_D0   40

bool sdReady = false;

// oled screen
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels 

// OLED and AHT10 must share this external I2C bus.
#define EXTERNAL_I2C_SDA 21
#define EXTERNAL_I2C_SCL 47

float tempC = 0, humiPct = 0;

#define MP2_Pin 14
#define Fire_Pin 3
#define BUZZER_PIN 1

#define TEMP_THRESHOLD_C 45.0f
#define HUMIDITY_MIN_THRESHOLD 30.0f
#define HUMIDITY_MAX_THRESHOLD 85.0f
#define GAS_THRESHOLD 500
#define FIRE_SENSOR_THRESHOLD 1500
#define FIRE_SENSOR_ACTIVE_LOW true

#define BUZZER_TONE_HZ 1000
#define BUZZER_ON_MS 250
#define BUZZER_OFF_MS 250
#define BUZZER_ALERT_DURATION_MS 30000

#define AI_ALERT_MIN_SAVE_INTERVAL_MS 5000
#define TZ_OFFSET_SECONDS 25200

// I2C clock speeds
#define I2C_CLOCK_CAMERA 100000
#define I2C_CLOCK_EXTERNAL 100000 // AHT10 and OLED share this bus
#define CAMERA_SCCB_I2C_PORT 0
#define EXTERNAL_I2C_PORT 1

TwoWire I2CCamera = TwoWire(CAMERA_SCCB_I2C_PORT);
TwoWire I2CExternal = TwoWire(EXTERNAL_I2C_PORT);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2CExternal, -1);
bool displayReady = false;

struct SensorSnapshot {
  float temperature;
  float humidity;
  int gasValue;
  int fireValue;
  bool sensorOk;
  unsigned long updatedAt;
};

SensorSnapshot latestSensorSnapshot = {0.0f, 0.0f, 0, 0, false, 0};
portMUX_TYPE sensorSnapshotMux = portMUX_INITIALIZER_UNLOCKED;

unsigned long buzzerAlertUntil = 0;
unsigned long lastBuzzerToggle = 0;
bool buzzerToneOn = false;
unsigned long lastAiAlertSavedAt = 0;

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

static bool ensureDirectory(const char* path) {
  if (!sdReady) return false;
  if (SD_MMC.exists(path)) return true;
  if (!SD_MMC.mkdir(path)) {
    Serial.printf("Failed to create folder: %s\n", path);
    return false;
  }
  return true;
}

static bool ensureAlertStorage() {
  return ensureDirectory("/image") && ensureDirectory("/log");
}

static void initTimeSync() {
  configTime(TZ_OFFSET_SECONDS, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 3000)) {
    char timeText[24];
    strftime(timeText, sizeof(timeText), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("Time synced: %s\n", timeText);
  } else {
    Serial.println("Time sync failed, will use web timestamp fallback for alert files");
  }
}

static String sanitizeFileToken(const String& value) {
  String sanitized;
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if ((c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') ||
        c == '_' ||
        c == '-') {
      sanitized += c;
    }
  }
  return sanitized;
}

static String makeTimestamp(const String& fallbackTimestamp) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 200)) {
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
    return String(timestamp);
  }

  String fallback = sanitizeFileToken(fallbackTimestamp);
  if (fallback.length() >= 13) {
    return fallback;
  }

  return "uptime_" + String(millis() / 1000);
}

static void updateSensorSnapshot(float temperature, float humidity, int gasValue, int fireValue, bool sensorOk) {
  portENTER_CRITICAL(&sensorSnapshotMux);
  if (sensorOk) {
    latestSensorSnapshot.temperature = temperature;
    latestSensorSnapshot.humidity = humidity;
  }
  latestSensorSnapshot.gasValue = gasValue;
  latestSensorSnapshot.fireValue = fireValue;
  latestSensorSnapshot.sensorOk = sensorOk;
  latestSensorSnapshot.updatedAt = millis();
  portEXIT_CRITICAL(&sensorSnapshotMux);
}

static SensorSnapshot getSensorSnapshot() {
  SensorSnapshot snapshot;
  portENTER_CRITICAL(&sensorSnapshotMux);
  snapshot = latestSensorSnapshot;
  portEXIT_CRITICAL(&sensorSnapshotMux);
  return snapshot;
}

static bool isFireSensorOverThreshold(int fireValue) {
  if (FIRE_SENSOR_ACTIVE_LOW) {
    return fireValue <= FIRE_SENSOR_THRESHOLD;
  }
  return fireValue >= FIRE_SENSOR_THRESHOLD;
}

static bool sensorsOverThreshold(const SensorSnapshot& snapshot) {
  bool temperatureDanger = snapshot.sensorOk && snapshot.temperature >= TEMP_THRESHOLD_C;
  bool humidityDanger = snapshot.sensorOk &&
                        (snapshot.humidity < HUMIDITY_MIN_THRESHOLD ||
                         snapshot.humidity > HUMIDITY_MAX_THRESHOLD);
  bool gasDanger = snapshot.gasValue >= GAS_THRESHOLD;
  bool fireDanger = isFireSensorOverThreshold(snapshot.fireValue);
  return temperatureDanger || humidityDanger || gasDanger || fireDanger;
}

static String getQueryValue(httpd_req_t* req, const char* key) {
  char query[512];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    return "";
  }

  char value[256];
  if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) {
    return "";
  }

  return String(value);
}

static bool queryFlag(httpd_req_t* req, const char* key) {
  String value = getQueryValue(req, key);
  value.toLowerCase();
  return value == "1" || value == "true" || value == "yes";
}

static bool saveRequestBodyToFile(httpd_req_t* req, const String& path) {
  if (!sdReady || req->content_len == 0) {
    return false;
  }

  File file = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!file) {
    Serial.printf("Failed to open image file for writing: %s\n", path.c_str());
    return false;
  }

  size_t remaining = req->content_len;
  uint8_t buffer[1024];
  while (remaining > 0) {
    size_t toRead = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    int received = httpd_req_recv(req, reinterpret_cast<char*>(buffer), toRead);
    if (received <= 0) {
      if (received == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      file.close();
      SD_MMC.remove(path.c_str());
      Serial.println("Failed while receiving AI image body");
      return false;
    }

    file.write(buffer, received);
    remaining -= received;
    yield();
  }

  file.close();
  return true;
}

static bool writeAiLogFile(
    const String& path,
    const String& timestamp,
    const String& alertType,
    bool cameraFireDetected,
    bool cameraSmokeDetected,
    bool sensorOverLimit,
    const String& edgeCase,
    const String& imagePath,
    const String& detections,
    const SensorSnapshot& snapshot
) {
  if (!sdReady) return false;

  File file = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!file) {
    Serial.printf("Failed to open log file for writing: %s\n", path.c_str());
    return false;
  }

  file.printf("timestamp=%s\n", timestamp.c_str());
  file.printf("alert_type=%s\n", alertType.c_str());
  file.printf("edge_case=%s\n", edgeCase.c_str());
  file.printf("camera_fire_detected=%s\n", cameraFireDetected ? "true" : "false");
  file.printf("camera_smoke_detected=%s\n", cameraSmokeDetected ? "true" : "false");
  file.printf("sensor_over_threshold=%s\n", sensorOverLimit ? "true" : "false");
  file.printf("sensor_ok=%s\n", snapshot.sensorOk ? "true" : "false");
  file.printf("temperature_c=%.2f\n", snapshot.temperature);
  file.printf("humidity_percent=%.2f\n", snapshot.humidity);
  file.printf("gas_value=%d\n", snapshot.gasValue);
  file.printf("fire_value=%d\n", snapshot.fireValue);
  file.printf("threshold_temperature_c=%.2f\n", TEMP_THRESHOLD_C);
  file.printf("threshold_humidity_min=%.2f\n", HUMIDITY_MIN_THRESHOLD);
  file.printf("threshold_humidity_max=%.2f\n", HUMIDITY_MAX_THRESHOLD);
  file.printf("threshold_gas=%d\n", GAS_THRESHOLD);
  file.printf("threshold_fire=%d\n", FIRE_SENSOR_THRESHOLD);
  file.printf("fire_sensor_active_low=%s\n", FIRE_SENSOR_ACTIVE_LOW ? "true" : "false");
  file.printf("image_path=%s\n", imagePath.c_str());
  file.printf("detections=%s\n", detections.c_str());

  file.close();
  return true;
}

static void triggerBuzzerAlert() {
  buzzerAlertUntil = millis() + BUZZER_ALERT_DURATION_MS;
}

static void stopBuzzer() {
  if (buzzerToneOn) {
    noTone(BUZZER_PIN);
    buzzerToneOn = false;
  }
}

static void handleBuzzerAlert() {
  unsigned long now = millis();
  if ((long)(now - buzzerAlertUntil) >= 0) {
    stopBuzzer();
    return;
  }

  unsigned long interval = buzzerToneOn ? BUZZER_ON_MS : BUZZER_OFF_MS;
  if (now - lastBuzzerToggle < interval) {
    return;
  }

  lastBuzzerToggle = now;
  if (buzzerToneOn) {
    noTone(BUZZER_PIN);
    buzzerToneOn = false;
  } else {
    tone(BUZZER_PIN, BUZZER_TONE_HZ);
    buzzerToneOn = true;
  }
}

static void sendJsonResponse(httpd_req_t* req, int statusCode, const String& body) {
  String status = String(statusCode) + " ";
  if (statusCode == 200) status += "OK";
  else if (statusCode == 400) status += "Bad Request";
  else if (statusCode == 500) status += "Internal Server Error";
  else status += "Error";

  httpd_resp_set_status(req, status.c_str());
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, body.c_str(), body.length());
}

static bool initCamera() {
  camera_config_t config = {};
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
  config.pin_sccb_sda = -1;
  config.pin_sccb_scl = -1;
  config.sccb_i2c_port = CAMERA_SCCB_I2C_PORT;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming


  // Tối ưu ổn định trước
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 15;
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

static esp_err_t ai_alert_options_handler(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t ai_alert_handler(httpd_req_t* req) {
  String alertType = getQueryValue(req, "alert_type");
  if (alertType.length() == 0) {
    alertType = "unknown";
  }

  bool cameraFireDetected = queryFlag(req, "has_fire");
  bool cameraSmokeDetected = queryFlag(req, "has_smoke");
  String detections = getQueryValue(req, "detections");
  String webTimestamp = getQueryValue(req, "captured_at");
  String timestamp = makeTimestamp(webTimestamp);
  SensorSnapshot snapshot = getSensorSnapshot();
  bool sensorOverLimit = sensorsOverThreshold(snapshot);

  String edgeCase = "camera_ai_update";
  if (cameraFireDetected && sensorOverLimit) {
    edgeCase = "camera_fire_sensor_threshold";
  } else if (cameraFireDetected) {
    edgeCase = "camera_fire_sensor_normal";
  } else if (cameraSmokeDetected) {
    edgeCase = "camera_smoke_only";
  }

  String imagePath = "";
  String logPath = "";
  bool imageSaved = false;
  bool logSaved = false;
  bool shouldPersistToSd = cameraFireDetected;
  bool canSaveNow = lastAiAlertSavedAt == 0 || millis() - lastAiAlertSavedAt >= AI_ALERT_MIN_SAVE_INTERVAL_MS;

  if (shouldPersistToSd && canSaveNow) {
    if (!ensureAlertStorage()) {
      sendJsonResponse(req, 500, "{\"ok\":false,\"error\":\"sd_storage_not_ready\"}");
      return ESP_OK;
    }

    String fileStem = timestamp + "_" + sanitizeFileToken(alertType);
    imagePath = "/image/" + fileStem + ".jpg";
    logPath = "/log/" + fileStem + ".txt";

    imageSaved = saveRequestBodyToFile(req, imagePath);
    logSaved = writeAiLogFile(
      logPath,
      timestamp,
      alertType,
      cameraFireDetected,
      cameraSmokeDetected,
      sensorOverLimit,
      edgeCase,
      imagePath,
      detections,
      snapshot
    );

    if (imageSaved || logSaved) {
      lastAiAlertSavedAt = millis();
    }
  } else {
    size_t remaining = req->content_len;
    char discardBuffer[256];
    while (remaining > 0) {
      size_t toRead = remaining < sizeof(discardBuffer) ? remaining : sizeof(discardBuffer);
      int received = httpd_req_recv(req, discardBuffer, toRead);
      if (received <= 0) break;
      remaining -= received;
      yield();
    }
  }

  if (cameraFireDetected && sensorOverLimit) {
    triggerBuzzerAlert();
  }

  bool firebaseUpdated = false;
  if (network != nullptr) {
    firebaseUpdated = network->firestoreAiDataUpdate(
      snapshot.temperature,
      snapshot.humidity,
      snapshot.gasValue,
      snapshot.fireValue,
      cameraFireDetected,
      cameraSmokeDetected,
      sensorOverLimit,
      alertType,
      edgeCase,
      imagePath,
      logPath,
      timestamp
    );
  }

  String response = "{";
  response += "\"ok\":true";
  response += ",\"edge_case\":\"";
  response += edgeCase;
  response += "\"";
  response += ",\"sensor_over_threshold\":";
  response += sensorOverLimit ? "true" : "false";
  response += ",\"image_saved\":";
  response += imageSaved ? "true" : "false";
  response += ",\"log_saved\":";
  response += logSaved ? "true" : "false";
  response += ",\"firebase_updated\":";
  response += firebaseUpdated ? "true" : "false";
  response += ",\"image_path\":\"";
  response += imagePath;
  response += "\"";
  response += ",\"log_path\":\"";
  response += logPath;
  response += "\"";
  response += "}";

  sendJsonResponse(req, 200, response);
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

  httpd_uri_t ai_alert_uri = {
      .uri = "/ai_alert",
      .method = HTTP_POST,
      .handler = ai_alert_handler,
      .user_ctx = NULL};

  httpd_uri_t ai_alert_options_uri = {
      .uri = "/ai_alert",
      .method = HTTP_OPTIONS,
      .handler = ai_alert_options_handler,
      .user_ctx = NULL};

  if (httpd_start(&s_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(s_httpd, &index_uri);
    httpd_register_uri_handler(s_httpd, &stream_uri);
    httpd_register_uri_handler(s_httpd, &ai_alert_uri);
    httpd_register_uri_handler(s_httpd, &ai_alert_options_uri);
    Serial.println("HTTP server started");
  } else {
    Serial.println("HTTP server start failed");
  }
}


void setup() {
  Serial.begin(115200);

  Serial.setDebugOutput(false); // Disable I2C debug spam
  delay(500);

  // Preconfigure SCCB so the camera driver reuses I2C controller 0.
  I2CCamera.begin(SIOD_GPIO_NUM, SIOC_GPIO_NUM, I2C_CLOCK_CAMERA);

  // Camera
  if (!initCamera()) {
    Serial.println("Camera init failed -> stop");
    while (true) delay(1000);
  }

  // Keep external I2C devices on controller 1.
  I2CExternal.begin(EXTERNAL_I2C_SDA, EXTERNAL_I2C_SCL, I2C_CLOCK_EXTERNAL);

  delay(500);

  //sensors
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if(!displayReady) {
    Serial.println(F("SSD1306 allocation failed"));
  }
  
  if (displayReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
  }

  // SAU (thay vào)
  Serial.println("AHT10 test!");
  if (!sensorInit(I2CExternal)) {
    Serial.println("AHT10 Init Failed - Check wiring!");
    if (displayReady) {
      display.setCursor(0, 0);
      display.println("AHT10 Failed");
      display.display();
    }
  } else {
    Serial.println("AHT10 OK");
  }

  analogSetAttenuation(ADC_11db);
  pinMode(BUZZER_PIN, OUTPUT);
  stopBuzzer();

  // Hiển thị trạng thái kết nối WiFi lên màn hình
  if (displayReady) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Connecting to Wi-Fi...");
    display.display();
  }

  // PSRAM check
  if (psramFound()) Serial.println("PSRAM: FOUND");
  else Serial.println("PSRAM: NOT FOUND");

  // SD card must be mounted before Network reads/writes WiFi config files.
  sdSetup();
  sdInfo();

  // Connect to Wi-Fi, or start the WiFi Manager AP when saved config fails.
  bool wifiConnected = initNetwork();
  if (wifiConnected) {
    initTimeSync();
    network->firebaseInit();

    // Server
    startCameraServer();
  } else if (displayReady) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi Manager AP");
    display.println("ESP-WIFI-MANAGER");
    display.print("IP: ");
    display.println(WiFi.softAPIP());
    display.display();
  }

  logMemory("BOOT");
}

void loop() {
  yield();
  handleBuzzerAlert();

  int gasValue = analogRead(MP2_Pin);
  int fireValue = analogRead(Fire_Pin);
  yield();

  float tempC = 0, humiPct = 0;
  bool sensor_ok = sensorRead(tempC, humiPct); // retry nằm trong SensorHandler
  updateSensorSnapshot(tempC, humiPct, gasValue, fireValue, sensor_ok);

  if (displayReady) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    if (!sensor_ok) {
      display.setCursor(0, 0);
      display.println("Sensor Error!");
      display.println("Gas:");
      display.println(gasValue);
      display.println("Fire:");
      display.println(fireValue);
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

      display.setCursor(70, 20);
      display.print(F("Fire: "));
      display.println(fireValue);
    }

    display.display();
  }
  yield();

  if (sensor_ok) {
    network->firestoreDataUpdate(tempC, humiPct, gasValue, fireValue);
  }

  for (int i = 0; i < 10; i++) {
    delay(100);
    handleBuzzerAlert();
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

bool initNetwork(){
  network = new Network();
  return network->initWiFi();
}
