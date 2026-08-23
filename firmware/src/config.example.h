#pragma once

// ---------------------------------------------------------------------------
// Configuration — COPIE ce fichier en "config.h" et remplis tes valeurs.
//   cp src/config.example.h src/config.h
//
// config.h est ignoré par git (voir .gitignore) : tes identifiants Wi-Fi n'y
// seront pas commités.
// ---------------------------------------------------------------------------

// --- Wi-Fi -----------------------------------------------------------------
#define WIFI_SSID     "TON_RESEAU_WIFI"
#define WIFI_PASSWORD "TON_MOT_DE_PASSE"

// --- Destination : le PC qui fait tourner le recepteur ---------------------
// Mets l'IP de ton PC sur le reseau local (ex. 192.168.1.42) et le port ecoute
// par ton recepteur. L'ESP32 et le PC doivent etre sur le MEME reseau.
#define PC_IP    "192.168.1.42"
#define PC_PORT  9000

// --- Reglages du flux ------------------------------------------------------
// Resolution : FRAMESIZE_QVGA (320x240) tient ~25 fps ; FRAMESIZE_VGA (640x480)
// est plus lourd (moins de fps). Commence petit.
#define FRAME_SIZE   FRAMESIZE_QVGA

// Qualite JPEG : 10 (haute qualite, gros) a 63 (basse, leger). 12 est un bon
// compromis. Plus c'est leger, plus le fps est tenable.
#define JPEG_QUALITY 12

// Cadence visee (images/seconde). Le firmware saute des trames s'il n'arrive
// pas a suivre, plutot que d'accumuler du retard (comme ton Pacer).
#define TARGET_FPS   25
