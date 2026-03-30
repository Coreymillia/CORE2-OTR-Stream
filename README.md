# M5Core2Radio 📻

WiFi OTR Internet Radio for **M5Stack Core2** — a port of [M5RadioStream](../M5RadioStream) (Core1 Basic) upgraded with **16-bit I2S audio** and an optional **SI4713 FM modulator** so the stream can be broadcast to any FM radio in the room.

Original concept: **[winRadio by Volos Projects](https://github.com/VolosR/WaveshareRadioStream)**

---

## Photos

### v1.3 — Core2 + CJMCU-4713 SI4713 FM Modulator wired up
![M5Core2Radio with SI4713 FM module](IMG_20260330_112507.jpg)
*Retro amber "M5 SHORTWAVE FM" skin — 1940s Radio playing, SI4713 module connected via jumper wires to Port A (I2C) and Port B (DAC audio)*

### FM broadcast in action — received on a Raddy shortwave radio
![FM broadcast received on Raddy radio](IMG_20260330_112658.jpg)
*The Core2 streams OTR internet radio and re-broadcasts it over FM. The Raddy is tuned to the SI4713's frequency and picking it up live. Ticker: "You Bet Your Life - Secret Word" (Groucho Marx, 1947)*

---

## Key Upgrade Over Core1 Version

| | Core1 Basic | **Core2** |
|---|---|---|
| Audio output | 8-bit internal DAC | **16-bit I2S → NS4168 amp** |
| Background hiss | Constant (hardware floor) | **Dramatically reduced** |
| Touch input | Physical buttons only | **Capacitive touch + physical** |
| Haptic feedback | None | **Vibration motor on every tap** |
| PSRAM | None | **4MB** |
| FM broadcast | None | **Optional SI4713 modulator** |

---

## Controls

Both the **on-screen touch footer** and **physical virtual buttons** work:

| Touch Zone | Button | Normal Mode | Settings Mode |
|---|---|---|---|
| [SET] | BtnA (short) | Open sound settings | — |
| — | **BtnA (hold 1s)** | **Toggle FM ↔ Speaker output** | — |
| [STA] | BtnB | Cycle station (1.5s debounce) | Select next parameter |
| [VOL] | BtnC (short) | Cycle volume 0–10 (0=mute) | Increase value |
| — | **BtnC (hold 1s)** | **Toggle screen on/off** | — |
| [BACK] | BtnA | — | Exit settings |

> **FM badge** in header: green = FM mode active, grey = speaker mode active.
> RDS song title updates automatically on the receiving radio as each show changes.

---

## Hardware

| Component | Details |
|-----------|---------|
| Board | M5Stack Core2 |
| MCU | ESP32 (dual-core, 240 MHz) |
| Display | ILI9341 320×240, capacitive touch |
| Audio amp | I2S → NS4168 (BCK=GPIO12, LRC=GPIO0, DOUT=GPIO2) |
| Speaker | 1W, 8Ω onboard |
| PSRAM | 4MB |
| FM module | CJMCU-4713 SI4713 *(optional)* |

---

## Optional SI4713 FM Modulator Wiring

The SI4713 is **fully optional** — the firmware detects it on I2C at boot and enables FM features automatically if present.

| CJMCU-4713 Pin | Core2 Connection | Notes |
|---|---|---|
| VIN | 5V | Module has onboard 3.3V regulator |
| GND | GND | |
| SDA | GPIO21 (Port A) | |
| SCL | GPIO22 (Port A) | |
| RST | GPIO13 (expansion) | Required — library toggles it |
| LIN | GPIO26 (Port B) | ESP32 DAC2 analog audio |
| RIN | GPIO26 (Port B) | Same pin — mono |
| CS/SEN | Module's own 3V0 pin | Sets I2C address to 0x63 |
| ANT | ~75cm wire | λ/4 for FM band ≈ 50m range |
| GP1, GP2, 3V0 | Unconnected | Not needed |

**FM frequency** is set in the sound settings menu (FM MHz) and persists in NVS across reboots.

---

## Stations (ROKiT Radio Network — OTR classics, 48 kbps MP3)

| # | Station | Highlights |
|---|---|---|
| 1 | 1940s Radio | Big band, wartime era |
| 2 | American Comedy | Fibber McGee & Molly, Jack Benny, You Bet Your Life |
| 3 | American Classics | Drama anthology |
| 4 | Jazz Central | Swing & jazz |
| 5 | Comedy Gold | Burns & Allen, Red Skelton |
| 6 | Mystery Radio | Suspense, Inner Sanctum |
| 7 | Crime & Suspense | Dragnet, Philip Marlowe |
| 8 | Crime Radio | Sam Spade, Boston Blackie |
| 9 | Adventure Stories | The Lone Ranger, Zorro |
| 10 | Drama Radio | Lux Radio Theatre |
| 11 | Nostalgia Lane | Mixed OTR variety |
| 12 | Science Fiction | X Minus One, Dimension X |

---

## Build & Flash

```bash
# Build and upload directly
pio run --target upload

# Serial monitor
pio device monitor --baud 115200
```

### Flash with M5Burner
Use **`M5Core2Radio-v1.3-MERGED.bin`** — flash to offset `0x0`.

---

## First Boot / WiFi Setup

On first boot (or hold **BtnA** during the 3-second splash), a captive portal opens:

1. Connect phone/PC to WiFi network **`M5Radio_Setup`**
2. Open browser → `192.168.4.1`
3. Enter your 2.4 GHz WiFi credentials → Save

Credentials are stored in NVS and survive power cycles.

---

## Planned Features

- **FM passthrough / aux input mode** — stop the internet stream but leave the SI4713 modulator active, allowing an external MP3 player or audio source to be plugged in and broadcast over FM. Turns the Core2 into a general-purpose FM transmitter.

---

## Credits

- **Original project:** [winRadio by Volos Projects](https://github.com/VolosR/WaveshareRadioStream)
- **Core2 port & skin:** CoreyMillia / GitHub Copilot — 2026
