// ---------------------------------------------------------------------------
// Firmware ESP32-CAM : capture une image JPEG, la fragmente selon le protocole
// PARTAGÉ du projet (cam/protocol.hpp), et l'envoie en UDP au récepteur PC.
//
// La boucle : capturer -> fragmenter -> envoyer -> cadencer. Simple, parce que
// l'OV2640 encode le JPEG en matériel : l'ESP32 ne calcule presque rien.
//
// NON compilable sur PC (framework Arduino / toolchain Xtensa) : se construit
// et se flashe avec PlatformIO. Voir README.md.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_camera.h"

#include "camera_pins.h"
#include "config.h"          // copie de config.example.h avec tes valeurs

#include "cam/protocol.hpp"  // MÊME en-tête / CRC que le récepteur (common/)

static WiFiUDP udp;
static std::uint32_t g_frame_id = 0;

// --- Initialisation de la caméra (config AI-Thinker, sortie JPEG) ----------
static bool init_camera() {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;  config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;  config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;  config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;  config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;  config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;  config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;   // <-- l'encodage se fait dans le capteur

    // Avec PSRAM : deux framebuffers + résolution/qualité demandées.
    if (psramFound()) {
        config.frame_size   = FRAME_SIZE;
        config.jpeg_quality = JPEG_QUALITY;
        config.fb_count     = 2;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
        config.grab_mode    = CAMERA_GRAB_LATEST;   // toujours la plus fraîche
    } else {
        // Repli sans PSRAM : plus petit, un seul buffer.
        config.frame_size   = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }

    const esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Echec init camera : 0x%x (brochage ? alimentation ?)\n", err);
        return false;
    }
    return true;
}

// --- Envoi d'une trame : fragmentation + UDP -------------------------------
// On N'appelle PAS cam::fragment() (qui allouerait des vecteurs) : sur un
// microcontrôleur, on construit chaque datagramme dans un tampon de pile et on
// l'envoie immédiatement. Mais on réutilise cam::crc32 et cam::write_header,
// donc les octets produits sont IDENTIQUES à ceux qu'attend le récepteur.
static void send_frame(const std::uint8_t* jpeg, std::size_t len) {
    const std::size_t count =
        (len == 0) ? 1 : (len + cam::MAX_PAYLOAD - 1) / cam::MAX_PAYLOAD;

    // micros() : horloge locale de l'ESP32 (uptime), sans rapport avec l'horloge
    // du PC. La "latence absolue" mesurée côté PC n'aura donc pas de sens sans
    // synchronisation d'horloge ; c'est la GIGUE et le FPS qui comptent (voir
    // README). Le champ existe et la structure reste correcte.
    const std::uint64_t ts = static_cast<std::uint64_t>(micros());

    std::uint8_t header[cam::HEADER_SIZE];
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t start = i * cam::MAX_PAYLOAD;
        const std::size_t l = (len > start)
            ? ((len - start < cam::MAX_PAYLOAD) ? (len - start) : cam::MAX_PAYLOAD)
            : 0;

        cam::Header h;
        h.frame_id       = g_frame_id;
        h.timestamp_us   = ts;
        h.frame_size     = static_cast<std::uint32_t>(len);
        h.fragment_count = static_cast<std::uint16_t>(count);
        h.fragment_index = static_cast<std::uint16_t>(i);
        h.payload_size   = static_cast<std::uint16_t>(l);
        h.payload_crc    = cam::crc32(jpeg + start, l);
        cam::write_header(h, header);

        udp.beginPacket(PC_IP, PC_PORT);
        udp.write(header, cam::HEADER_SIZE);
        if (l > 0) {
            udp.write(jpeg + start, l);
        }
        udp.endPacket();
    }
    ++g_frame_id;
}

void setup() {
    Serial.begin(115200);
    Serial.println("\nESP32-CAM demarrage...");

    if (!init_camera()) {
        // Sans caméra, rien à faire : on clignote l'erreur à l'infini.
        while (true) { delay(1000); }
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connexion a %s ", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnecte. IP ESP32 : %s  ->  envoi vers %s:%d\n",
                  WiFi.localIP().toString().c_str(), PC_IP, PC_PORT);

    udp.begin(0);   // port local éphémère (on n'émet que)
}

void loop() {
    // Cadence à pas de temps fixe, même logique que ton Pacer : on vise un
    // créneau régulier ; si on est en retard, on se resynchronise sans rafale.
    static std::uint32_t next_deadline = millis();
    const std::uint32_t period_ms = (TARGET_FPS > 0) ? (1000u / TARGET_FPS) : 40u;

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
        if (fb->format == PIXFORMAT_JPEG) {
            send_frame(fb->buf, fb->len);
        }
        esp_camera_fb_return(fb);   // TRÈS important : rendre le buffer, sinon fuite
    }

    next_deadline += period_ms;
    const std::int32_t wait = static_cast<std::int32_t>(next_deadline - millis());
    if (wait > 0) {
        delay(static_cast<std::uint32_t>(wait));
    } else {
        next_deadline = millis();   // en retard : on repart d'ici, pas de rafale
    }
}
