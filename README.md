# M5Core2Radio 📻

WiFi OTR Internet Radio for **M5Stack Core2** — a port of [M5RadioStream](../M5RadioStream) (Core1 Basic) upgraded with **16-bit I2S audio** through the Core2's onboard NS4168 amplifier.

Original concept: **[winRadio by Volos Projects](https://github.com/VolosR/WaveshareRadioStream)**

---

## Key Upgrade Over Core1 Version

| | Core1 Basic | **Core2** |
|---|---|---|
| Audio output | 8-bit internal DAC | **16-bit I2S → NS4168 amp** |
| Background hiss | Constant (hardware floor) | **Dramatically reduced** |
| Touch input | Physical buttons only | **Capacitive touch + physical** |
| Haptic feedback | None | **Vibration motor on every tap** |
| PSRAM | None | **4MB** |

---

## Controls

Both the **on-screen touch footer** and **physical virtual buttons** work:

| Touch Zone | BtnA/B/C | Normal Mode | Settings Mode |
|---|---|---|---|
| [SET] | BtnA | Open sound settings | — |
| [STA] | BtnB | Cycle station | Select next parameter |
| [VOL] | BtnC | Cycle volume (0–10) | Increase value |
| [BACK] | BtnA | — | Exit settings |

> Volume 0 = mute. Haptic pulse on every touch event.

---

## Hardware

| Component | Details |
|-----------|---------|
| Board | M5Stack Core2 |
| MCU | ESP32 (dual-core, 240 MHz) |
| Display | ILI9341 320×240, capacitive touch |
| Audio | I2S → NS4168 amp (BCK=GPIO12, LRC=GPIO0, DOUT=GPIO2) |
| Speaker | 1W, 8Ω onboard |
| PSRAM | 4MB |

---

## Stations (ROKiT Radio Network — 48 kbps MP3)

1. 1940s Radio
2. American Classics
3. Jazz Central
4. Comedy Gold
5. Crime Radio
6. Nostalgia Lane
7. British Comedy
8. Science Fiction

---

## Build & Flash

```bash
pio run --target upload
pio device monitor
```

### Flash with M5Burner
Use `M5Core2Radio-v1.0-MERGED.bin` — flash to offset `0x0`.

---

## First Boot / WiFi Setup

On first boot (or hold **BtnA** during the 3-second splash), a captive portal opens:

1. Connect phone/PC to WiFi network **`M5Radio_Setup`**
2. Open browser → `192.168.4.1`
3. Enter your 2.4 GHz WiFi credentials → Save

Credentials are stored in NVS and survive power cycles.

---

## Credits

- **Original project:** [winRadio by Volos Projects](https://github.com/VolosR/WaveshareRadioStream)
- **Port & skin:** CoreyMillia — VOLOS / COPILOT SKIN 2026
