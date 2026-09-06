#pragma once

// ---------------------------------------------------------------------------
// Camera pinout of the ESP32-CAM AI-Thinker (the most common model, with the
// OV2640). These are the official values from the Espressif example.
//
// If you have a DIFFERENT board (ESP-EYE, M5Camera, TTGO...), this pinout
// differs: get yours from Espressif's CameraWebServer example and replace this
// block. A wrong pinout = camera init failure (error message at boot).
// ---------------------------------------------------------------------------
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
