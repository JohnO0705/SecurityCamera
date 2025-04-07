#include <ArduinoJson.h>
#include <ArduinoJson.hpp>

#include "arduino_secrets.h"
/*
  Discord WebHook Example for ESP32
*/
#include "esp_camera.h"
#define CAMERA_MODEL_WROVER_KIT
#include "camera_pins.h"
#include <FS.h>
#include "soc/soc.h"           // Disable brownour problems
#include "soc/rtc_cntl_reg.h"  // Disable brownour problems
#include "driver/rtc_io.h"
#include <EEPROM.h> 
#include "Freenove_WS2812_Lib_for_ESP32.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>



#define LEDS_COUNT  8
#define LEDS_PIN	2
#define CHANNEL		0

Freenove_ESP32_WS2812 strip = Freenove_ESP32_WS2812(LEDS_COUNT, LEDS_PIN, CHANNEL, TYPE_GRB);

const int PIN_TO_SENSOR = 32; // GPIO19 pin connected to OUTPUT pin of sensor
int pinStateCurrent   = LOW;  // current state of pin
int pinStatePrevious  = LOW;

const char* ssid = "ESP32TestStuff";
const char* password = "12345678";
const char* discordWebhookURL = "https://discordapp.com/api/webhooks/1306729488221999154/-LzI7Z9CRXOFSLYUURoNYhKV9n1W4WVgiiTTYWtUc9SrNRJxVn6OZJ8wPvF9hTnrR74j";


void setup() {
  Serial.begin(115200);
 WiFi.begin(ssid, password);
 while (WiFi.status() != WL_CONNECTED) {
    delay(500);
      Serial.print(".");
 }
    Serial.println("\nWiFi connected");
  pinMode(PIN_TO_SENSOR, INPUT);

  strip.begin();


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
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

}

void loop() {
  pinStatePrevious = pinStateCurrent; // store old state
  pinStateCurrent = digitalRead(PIN_TO_SENSOR);   // read new state

  if (pinStatePrevious == LOW && pinStateCurrent == HIGH) {   // pin state change: LOW -> HIGH
    Serial.println("Motion detected!");
    int randomNum = random(2);
    for (int i = 0; i < LEDS_COUNT; i++) {
      if(randomNum == 0){
        strip.setLedColorData(i, 225, 0, 0);
      }else if(randomNum == 1){
        strip.setLedColorData(i, 0, 225, 0);
      }else{
        strip.setLedColorData(i, 0, 0, 255);
      }
    }
    strip.show();
    capturePhoto();
    delay(1000);
  }
  else
  if (pinStatePrevious == HIGH && pinStateCurrent == LOW) {   // pin state change: HIGH -> LOW
    Serial.println("Motion stopped!");
    for (int i = 0; i < LEDS_COUNT; i++) {
      strip.setLedColorData(i, 0, 0, 0); // RGB color format (Red, Green, Blue)
    }
    strip.show();
    delay(10000);
  }
}

// Capture Photo
void capturePhoto() {

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  File file = SPIFFS.open("/image.jpg", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }else{
    file.write(fb->buf, fb->len);
    file.close();
  }

  int fileSize = file.size();
  http.addHeader("Content-Length", String(fileSize));

  http.begin(client, discordWebhookURL);
  String boundary = "boundary123";
  String endBoundary = "\r\n--" + boundary + "--\r\n";

  String headers = "--" + boundary + "\r\n";
  headers += "Content-Disposition: form-data; name=\"file\"; filename=\"image.jpg\"\r\n";
  headers += "Content-Type: image/jpeg\r\n\r\n";

  int contentLength = headers.length() + fb->len + endBoundary.length();

  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
  http.addHeader("Content-Length", String(contentLength));

  uint8_t *payload = (uint8_t *)malloc(contentLength);
    if (payload == nullptr) {
        Serial.println("Failed to allocate memory for payload");
        esp_camera_fb_return(fb);
        http.end();
        return;
    }

    // Copy headers to the payload
    memcpy(payload, headers.c_str(), headers.length());

    // Copy image data to the payload
    memcpy(payload + headers.length(), fb->buf, fb->len);

    // Copy the end boundary to the payload
    memcpy(payload + headers.length() + fb->len, endBoundary.c_str(), endBoundary.length());

  int httpCode = http.POST(payload, contentLength);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Discord response: " + response);
  } else {
    Serial.println("Error sending image: " + String(httpCode));
  }
  esp_camera_fb_return(fb);
  http.end();

}

