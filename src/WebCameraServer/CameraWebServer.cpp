#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "esp_http_server.h"

//
// WARNING!!! PSRAM IC required for UXGA resolution and high JPEG quality
//            Ensure ESP32 Wrover Module or other board with PSRAM is selected
//            Partial images will be transmitted if image exceeds buffer size
//
//            You must select partition scheme from the board menu that has at least 3MB APP space.
//            Face Recognition is DISABLED for ESP32 and ESP32-S2, because it takes up from 15 
//            seconds to process single frame. Face Detection is ENABLED if PSRAM is enabled as well

#include "board_config.h"

// ===========================
// Enter your WiFi credentials
// ===========================
const char* ssid = "HIEU";
const char* password = "31072004";

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

  config.xclk_freq_hz = 40000000;
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
  const char* html =
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>ESP32-S3 CAM</title></head><body style='font-family:Arial;'>"
      "<h2>ESP32-S3 Camera Web Server</h2>"
      "<p>Mo stream: <a href='/stream'>/stream</a></p>"
      "<img src='/stream' style='max-width:100%;height:auto;'/>"
      "</body></html>";

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
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

  // PSRAM check
  if (psramFound()) Serial.println("PSRAM: FOUND");
  else Serial.println("PSRAM: NOT FOUND");

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

  // Camera
  if (!initCamera()) {
    Serial.println("Camera init failed -> stop");
    while (true) delay(1000);
  }

  // Server
  startCameraServer();

  logMemory("BOOT");
}

void loop() {
  // In log nhe moi 5s de theo doi memory (tranh spam Serial)
  static uint32_t last = 0;
  if (millis() - last >= 5000) {
    last = millis();
    logMemory("RUN");
  }

  delay(10);
}
