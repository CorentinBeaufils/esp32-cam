#pragma once

// ---------------------------------------------------------------------------
// Brochage caméra de l'ESP32-CAM AI-Thinker (le modèle le plus répandu, avec
// l'OV2640). Ce sont les valeurs officielles de l'exemple Espressif.
//
// Si tu as une AUTRE carte (ESP-EYE, M5Camera, TTGO...), ce brochage diffère :
// récupère le tien dans l'exemple CameraWebServer d'Espressif et remplace ce
// bloc. Un mauvais brochage = échec d'init caméra (message d'erreur au boot).
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
