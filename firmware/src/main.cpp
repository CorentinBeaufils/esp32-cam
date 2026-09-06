// ---------------------------------------------------------------------------
// ESP32-CAM firmware: captures a JPEG image, fragments it using the project's
// SHARED protocol (cam/protocol.hpp), and sends it over UDP to the PC receiver.
//
// The loop: capture -> fragment -> send -> pace. Simple, because the OV2640
// encodes the JPEG in hardware: the ESP32 does almost no computation.
//
// NOT buildable on a PC (Arduino framework / Xtensa toolchain): build and flash
// it with PlatformIO. See README.md.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_camera.h"

#include "camera_pins.h"
#include "config.h"          // copy of config.example.h with your own values

#include "cam/protocol.hpp"  // SAME header / CRC as the receiver (common/)

static WiFiUDP udp;
static std::uint32_t g_frame_id = 0;

// --- Camera initialisation (AI-Thinker config, JPEG output) ----------------
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
    config.pixel_format = PIXFORMAT_JPEG;   // <-- encoding happens in the sensor

    // With PSRAM: two framebuffers + requested resolution/quality.
    if (psramFound()) {
        config.frame_size   = FRAME_SIZE;
        config.jpeg_quality = JPEG_QUALITY;
        config.fb_count     = 2;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
        config.grab_mode    = CAMERA_GRAB_LATEST;   // always the freshest one
    } else {
        // Fallback without PSRAM: smaller, single buffer.
        config.frame_size   = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }

    const esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x (wiring? power?)\n", err);
        return false;
    }
    return true;
}

// --- Sending one frame: fragmentation + UDP --------------------------------
// We do NOT call cam::fragment() (which would allocate vectors): on a
// microcontroller we build each datagram in a stack buffer and send it right
// away. But we reuse cam::crc32 and cam::write_header, so the bytes produced are
// IDENTICAL to what the receiver expects.
static void send_frame(const std::uint8_t* jpeg, std::size_t len) {
    const std::size_t count =
        (len == 0) ? 1 : (len + cam::MAX_PAYLOAD - 1) / cam::MAX_PAYLOAD;

    // micros(): the ESP32's local clock (uptime), unrelated to the PC's clock.
    // The "absolute latency" measured on the PC therefore has no meaning without
    // clock synchronisation; JITTER and FPS are what matter (see README). The
    // field exists and the structure stays correct.
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
    Serial.println("\nESP32-CAM starting...");

    if (!init_camera()) {
        // No camera, nothing to do: blink the error forever.
        while (true) { delay(1000); }
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setSleep(false);
    Serial.printf("Connecting to %s ", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected. ESP32 IP: %s  ->  sending to %s:%d\n",
                  WiFi.localIP().toString().c_str(), PC_IP, PC_PORT);

    udp.begin(0);   // ephemeral local port (we only send)
}

void loop() {
    // Fixed time-step pacing, same logic as the Pacer: aim for a regular slot;
    // if we fall behind, resynchronise without bursting.
    static std::uint32_t next_deadline = millis();
    const std::uint32_t period_ms = (TARGET_FPS > 0) ? (1000u / TARGET_FPS) : 40u;

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
        if (fb->format == PIXFORMAT_JPEG) {
            send_frame(fb->buf, fb->len);
        }
        esp_camera_fb_return(fb);   // VERY important: return the buffer or leak
    }

    // Serial heartbeat: every ~2 s, uptime + frames sent + RSSI (WiFi signal
    // strength in dBm: ~-50 = excellent, ~-80 = weak). A sign of life when the
    // ESP32 is wired to the PC; silent on a power bank (expected). RSSI is a
    // bonus for the distance scenarios (worth noting per position).
    static std::uint32_t last_log = 0;
    if (millis() - last_log >= 2000) {
        last_log = millis();
        Serial.printf("[esp] up=%lus  frames=%lu  RSSI=%d dBm\n",
                      static_cast<unsigned long>(millis() / 1000),
                      static_cast<unsigned long>(g_frame_id),
                      static_cast<int>(WiFi.RSSI()));
    }

    next_deadline += period_ms;
    const std::int32_t wait = static_cast<std::int32_t>(next_deadline - millis());
    if (wait > 0) {
        delay(static_cast<std::uint32_t>(wait));
    } else {
        next_deadline = millis();   // behind: restart from here, no burst
    }
}
