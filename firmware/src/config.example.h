#pragma once

// ---------------------------------------------------------------------------
// Configuration - COPY this file to "config.h" and fill in your values.
//   cp src/config.example.h src/config.h
//
// config.h is ignored by git (see .gitignore): your Wi-Fi credentials will not
// be committed.
// ---------------------------------------------------------------------------

// --- Wi-Fi -----------------------------------------------------------------
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// --- Destination: the PC running the receiver ------------------------------
// Set the IP of your PC on the local network (e.g. 192.168.1.42) and the port
// your receiver listens on. The ESP32 and the PC must be on the SAME network.
#define PC_IP    "192.168.1.42"
#define PC_PORT  9000

// --- Stream settings -------------------------------------------------------
// Resolution: FRAMESIZE_QVGA (320x240) holds ~25 fps; FRAMESIZE_VGA (640x480)
// is heavier (fewer fps). Start small.
#define FRAME_SIZE   FRAMESIZE_QVGA

// JPEG quality: 10 (high quality, large) to 63 (low, light). 12 is a good
// compromise. The lighter it is, the more sustainable the fps.
#define JPEG_QUALITY 12

// Target rate (frames/second). The firmware drops frames if it can't keep up,
// rather than accumulating lag (like your Pacer).
#define TARGET_FPS   25
