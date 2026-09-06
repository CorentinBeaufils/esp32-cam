# Phase 2 - ESP32-CAM Firmware

The "real ESP32": it captures JPEG images and sends them over UDP to the PC
receiver, using **the same protocol** as the simulator (`common/cam/protocol.hpp`).
Your receiver (`viewer` / `receiver`) has **nothing to change** - it doesn't know
whether it's talking to the board or to the simulator.

> ⚠️ **Code provided, not tested by the assistant.** The firmware does not compile
> on a PC (Arduino framework, Xtensa toolchain). It relies on the official
> ESP32-CAM AI-Thinker patterns, but you'll be the first to flash it. Expect to
> adjust one or two things (pinout if a different board, settings).

---

## Hardware

- One **ESP32-CAM AI-Thinker** (OV2640) - the most common one.
- One **USB-serial adapter** (FTDI/CP2102) at 3.3 V for flashing: the ESP32-CAM
  has no USB.
- A **solid 5 V power supply**: the camera + Wi-Fi draw current spikes; a weak
  supply causes reboots or init failures.

## Wiring for flashing (USB-serial adapter <-> ESP32-CAM)

| USB-serial | ESP32-CAM |
|---|---|
| 5V | 5V |
| GND | GND |
| TX | U0R (RX) |
| RX | U0T (TX) |
| — | **GPIO0 <-> GND** (only to enter flash mode) |

Sequence: tie **GPIO0 to GND**, press RESET (or power-cycle), start the flash.
Once flashed, **disconnect GPIO0 from GND** and RESET to run.

## Configuration

```bash
cp src/config.example.h src/config.h
```

Edit `src/config.h`:
- `WIFI_SSID` / `WIFI_PASSWORD` - your network (2.4 GHz: the ESP32 does not do 5 GHz);
- `PC_IP` - the IP of your PC on the local network (e.g. `192.168.1.42`), **not**
  `127.0.0.1`: the ESP32 must reach your PC over the network;
- `PC_PORT` - your receiver's port (default 9000);
- `FRAME_SIZE`, `JPEG_QUALITY`, `TARGET_FPS` - start small (QVGA, quality 12,
  25 fps).

`config.h` is gitignored: your credentials do not go into git.

## Build and flash

With PlatformIO (in WSL or the VS Code extension):

```bash
cd firmware
pio run                 # compile
pio run -t upload       # flash (GPIO0 to GND, see above)
pio device monitor      # serial logs (115200 baud)
```

At boot, the monitor shows the IP obtained and the send target.

## Running the full chain

```bash
# On the PC: the receiver, on the chosen port
./build/receiver/receiver 9000        # or ./build/display/viewer 9000

# The ESP32, once flashed and powered, emits on its own.
```

Open the PC's **firewall** for this UDP port, and check that the PC and the ESP32
are on the **same subnet**. The receiver should show an fps close to `TARGET_FPS`
and, with the `viewer`, **finally some real images** (where the simulator only
sent synthetic noise).

---

## Two points of technical honesty

**Absolute latency will be meaningless.** The `timestamp_us` field is filled with
the ESP32's `micros()` (its uptime), a **different** clock from the PC's. Without
clock synchronization (NTP/PTP), the "latency" computed on the PC side is an
arbitrary offset, not a real delay. What remains **valid and useful**: the
**fps**, the **jitter** (variation, independent of the offset), and the
**losses / corruptions**. For true latency, you would need to synchronize the
clocks - that's a possible refinement for later.

**The protocol is copied into `src/`.** To keep things simple and robust,
`src/cam/protocol.hpp` and `src/protocol.cpp` are **copies** of `common/`.
PlatformIO compiles all of `src/` with no special configuration. Drawback: two
copies to keep in sync - but the protocol is frozen, so the risk is low. If you
modify the protocol on the PC side, re-copy the two files:

```bash
cp ../common/include/cam/protocol.hpp src/cam/protocol.hpp
cp ../common/src/protocol.cpp         src/protocol.cpp
```

(A symbolic link avoids the duplication if your system allows it.)

---

## Troubleshooting

| Symptom | Lead |
|---|---|
| `Echec init camera : 0x...` at boot | pinout (different board?), or 5 V supply too weak |
| Reboot loop (brownout) | insufficient power: better supply / better wires |
| Wi-Fi won't connect | 2.4 GHz network? SSID/password? range? |
| The receiver gets nothing | `PC_IP` correct? same subnet? UDP firewall open? |
| Very low fps | lower the resolution / raise `JPEG_QUALITY` (larger value = lighter) |
| Lots of losses at the receiver | Wi-Fi saturated/weak; move the board closer to the access point |
