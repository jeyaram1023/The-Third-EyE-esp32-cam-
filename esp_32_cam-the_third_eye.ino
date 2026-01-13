#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h" 
#include "soc/rtc_cntl_reg.h" 
#include "driver/rtc_io.h" 
#include "esp_http_server.h"
#include <ESP32Servo.h>
#include "SD_MMC.h" // Library for SD Card
#include "FS.h"     // File System

// ===================
// 1. SETTINGS
// ===================
const char* ssid = "Airtel_Venkateswarlu";                 // <--- CHANGE THIS 'Airtel_Venkateswarlu'
const char* password = "Akkasai@52";   // <--- CHANGE THIS       'Akkasai@52'

// Offline Mode Settings
#define TIME_LAPSE_INTERVAL 5000  // Take photo every 5 seconds

// ===================
// 2. PIN DEFINITIONS
// ===================
#define PAN_PIN       13  
#define TILT_PIN      12  
#define FLASH_LED_PIN 4   
#define RED_LED_PIN   33  

// Camera Pins (AI Thinker Model)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

Servo panServo;
Servo tiltServo;
httpd_handle_t camera_httpd = NULL;
bool isOfflineMode = false;
int pictureNumber = 0;

// ===================
// 3. HTML PAGE
// ===================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Helvetica; text-align: center; margin:0; padding-top: 20px; background-color: #222; color: white; }
    
    .video-container {
      position: relative; display: inline-block; border: 4px solid #444; border-radius: 8px; overflow: hidden; background-color: #000;
    }
    img { display: block; max-width: 100%; height: auto; }

    .section-title { margin-top: 20px; color: #aaa; font-size: 14px; text-transform: uppercase; letter-spacing: 1px; }
    .slider-container { margin: 10px auto; width: 90%; max-width: 400px; }
    label { font-size: 1.1rem; display: block; margin-bottom: 5px; }
    
    input[type=range] { -webkit-appearance: none; width: 100%; height: 10px; border-radius: 5px; background: #555; outline: none; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 20px; height: 20px; border-radius: 50%; background: #4CAF50; cursor: pointer; }

    button { background-color: #FFFF00; border: none; color: black; padding: 15px 32px; text-align: center; display: inline-block; font-size: 16px; margin: 15px; cursor: pointer; border-radius: 8px; font-weight: bold; }
    .btn-on { background-color: #f44336; color: white; } 
    .btn-off { background-color: #FFFF00; color: black; }
    
    /* Specific colors for setting sliders */
    #quality::-webkit-slider-thumb { background: #2196F3; }
    #brightness::-webkit-slider-thumb { background: #FF9800; }

    /* Footer Styling */
    .footer {
      margin-top: 40px;
      padding-bottom: 20px;
      font-size: 12px;
      color: #888;
      border-top: 1px solid #444;
      padding-top: 10px;
      width: 90%;
      margin-left: auto;
      margin-right: auto;
    }
  </style>
</head>
<body>
  <h3>The Third EyE</h3>
  <div class="video-container">
    <img src="/stream" id="photo" alt="Video Stream">
  </div>
  
  <div>
    <button id="ledBtn" class="btn-off" onclick="toggleLED()">Turn Flash ON</button>
  </div>

  <div class="section-title">Robot Movement</div>
  <div class="slider-container">
    <label>Pan (Left/Right)</label>
    <input type="range" min="0" max="180" value="90" oninput="sendData('pan', this.value)">
  </div>
  <div class="slider-container">
    <label>Tilt (Up/Down)</label>
    <input type="range" min="0" max="180" value="90" oninput="sendData('tilt', this.value)">
  </div>

  <div class="section-title">Camera Settings</div>
  <div class="slider-container">
    <label>Brightness</label>
    <input id="brightness" type="range" min="-2" max="2" value="0" step="1" onchange="sendData('brightness', this.value)">
  </div>
  <div class="slider-container">
    <label>Quality (Low # is Better)</label>
    <input id="quality" type="range" min="10" max="63" value="12" step="1" onchange="sendData('quality', this.value)">
  </div>

  <div class="footer">
    &copy; Design Jayram studio 2026
  </div>

  <script>
    var ledState = 0;
    function sendData(variable, val) {
      var xhttp = new XMLHttpRequest();
      xhttp.open("GET", "/control?var=" + variable + "&val=" + val, true);
      xhttp.send();
    }
    function toggleLED() {
      ledState = !ledState;
      var btn = document.getElementById("ledBtn");
      if (ledState) {
        btn.innerText = "Turn Flash OFF";
        btn.className = "btn-on";
        sendData('led', 1);
      } else {
        btn.innerText = "Turn Flash ON";
        btn.className = "btn-off";
        sendData('led', 0);
      }
    }
    document.getElementById("photo").onload = function() { };
    document.getElementById("photo").onerror = function() {
       setTimeout(function(){ document.getElementById("photo").src = "/stream?t=" + new Date().getTime(); }, 1000);
    };
  </script>
</body>
</html>
)rawliteral";

// ===================
// 4. HANDLERS
// ===================

static esp_err_t index_handler(httpd_req_t *req){
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_handler(httpd_req_t *req){
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
  if(res != ESP_OK) return res;

  while(true){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if(fb->format != PIXFORMAT_JPEG){
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if(!jpeg_converted){
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, "\r\n--frame\r\n", 12);
    }
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if(res != ESP_OK){
      break;
    }
  }
  return res;
}

static esp_err_t cmd_handler(httpd_req_t *req){
  char* buf;
  size_t buf_len;
  char variable[32] = {0,};
  char value[32] = {0,};

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if(!buf){ httpd_resp_send_500(req); return ESP_FAIL; }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
          httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
      } else { free(buf); httpd_resp_send_404(req); return ESP_FAIL; }
    } else { free(buf); httpd_resp_send_404(req); return ESP_FAIL; }
    free(buf);
  } else { httpd_resp_send_404(req); return ESP_FAIL; }

  int val = atoi(value);
  sensor_t * s = esp_camera_sensor_get(); // Get access to camera settings

  if(!strcmp(variable, "pan")) {
    panServo.write(val);
  }
  else if(!strcmp(variable, "tilt")) {
    tiltServo.write(val);
  }
  else if(!strcmp(variable, "led")) {
    pinMode(FLASH_LED_PIN, OUTPUT); 
    digitalWrite(FLASH_LED_PIN, val); 
  }
  else if(!strcmp(variable, "brightness")) {
    s->set_brightness(s, val);  
  }
  else if(!strcmp(variable, "quality")) {
    s->set_quality(s, val);  
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

void startCameraServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
  httpd_uri_t cmd_uri = { .uri = "/control", .method = HTTP_GET, .handler = cmd_handler, .user_ctx = NULL };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &stream_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
  }
}

// Save picture to SD Card
void savePictureToSD() {
  camera_fb_t * fb = esp_camera_fb_get();
  if(!fb) { Serial.println("Camera Capture Failed"); return; }
  
  String path = "/picture" + String(pictureNumber) + ".jpg";
  Serial.printf("Saving picture: %s\n", path.c_str());

  fs::FS &fs = SD_MMC;
  File file = fs.open(path.c_str(), FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file for writing");
  } else {
    file.write(fb->buf, fb->len);
    Serial.println("Saved.");
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(50);
    digitalWrite(FLASH_LED_PIN, LOW);
  }
  file.close();
  esp_camera_fb_return(fb);
  pictureNumber++;
}

void blinkLED(int pin, int times, int delayTime, bool invert) {
  pinMode(pin, OUTPUT);
  for(int i=0; i<times; i++) {
    if(invert) digitalWrite(pin, LOW); else digitalWrite(pin, HIGH);
    delay(delayTime);
    if(invert) digitalWrite(pin, HIGH); else digitalWrite(pin, LOW);
    delay(delayTime);
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);

  // === LED SETUP ===
  rtc_gpio_hold_dis(GPIO_NUM_4); 
  pinMode(FLASH_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW); 
  digitalWrite(RED_LED_PIN, HIGH); 

  // Camera Configuration
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA; 
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // === CHECK SD CARD (1-bit mode) ===
  if(SD_MMC.begin("/sdcard", true)){
    Serial.println("SD Card Detected! Entering OFFLINE Mode.");
    isOfflineMode = true;
    digitalWrite(RED_LED_PIN, LOW); 
    
    while(SD_MMC.exists("/picture" + String(pictureNumber) + ".jpg")) {
      pictureNumber++;
    }
  } 
  else {
    Serial.println("No SD Card. Entering ONLINE Mode.");
    isOfflineMode = false;

    panServo.setPeriodHertz(50); 
    panServo.attach(PAN_PIN, 1000, 2000);
    tiltServo.setPeriodHertz(50);
    tiltServo.attach(TILT_PIN, 1000, 2000);
    
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println(" Connected!");
    
    blinkLED(RED_LED_PIN, 2, 200, true);
    blinkLED(FLASH_LED_PIN, 2, 100, false); 
    delay(200);
    blinkLED(FLASH_LED_PIN, 2, 100, false); 
    blinkLED(RED_LED_PIN, 2, 500, true);

    Serial.print("Camera Stream Ready: http://");
    Serial.println(WiFi.localIP());
    startCameraServer();
  }
}

void loop() {
  if (isOfflineMode) {
    savePictureToSD();
    delay(TIME_LAPSE_INTERVAL);
  } else {
    delay(10000);
  }
}