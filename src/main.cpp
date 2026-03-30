/**
 * M5Core2Radio — WiFi OTR Internet Radio for M5Stack Core2
 *
 * Port of M5RadioStream (Core1 Basic) to M5Stack Core2.
 * Original winRadio concept by Volos Projects.
 *
 * Key upgrade over Core1: I2S digital audio → NS4168 amp (16-bit, no DAC hiss)
 * I2S pins: BCK=GPIO12, LRC=GPIO0, DOUT=GPIO2
 *
 * Optional SI4713 FM modulator (CJMCU-4713):
 *   RST=GPIO13, SDA=GPIO21, SCL=GPIO22 (Port A)
 *   LIN=GPIO26, RIN=GPIO26 (Port B, ESP32 DAC2)
 *   SEN/CS → module's own 3V0 pin (3.3V, sets I2C addr 0x63)
 *
 * Controls — touch footer zones AND physical virtual buttons both work:
 *   [SET] / BtnA  = Open sound settings  (short press)
 *                   Toggle FM ↔ Speaker   (hold 1 second, only when FM module present)
 *   [STA] / BtnB  = Cycle station
 *   [VOL] / BtnC  = Cycle volume (0–10, 0=mute)
 *
 * Sound settings mode:
 *   [BACK] / BtnA = Exit
 *   [SEL]  / BtnB = Next parameter (Volume → Bass → Treble)
 *   [+]    / BtnC = Increase value (wraps)
 *
 * Haptic: vibration motor pulses on every touch event (AXP192 LDO3)
 * WiFi:   credentials stored in NVS via captive portal
 *         Hold BtnC (VOL) 1 second to toggle screen on/off
 */

#include <M5Core2.h>
#include <WiFiMulti.h>
#include <Audio.h>
#include <Adafruit_Si4713.h>
#include <Preferences.h>
#include <driver/i2s.h>
#include "Portal.h"

#define SCREEN_W  320
#define SCREEN_H  240

// ─── Retro amber palette ─────────────────────────────────────────────────────
#define SW_AMBER    0xFAE0
#define SW_AMBER_D  0x7100
#define SW_HDR_BG   0x1082
#define SW_BTN_BG   0x2126
#define SW_SLOT_BG  0x0841
#define SW_GRN_DIM  0x0180
#define SW_DGREY    0x2945

// ─── Layout Y positions ──────────────────────────────────────────────────────
#define SW_LINE1     24
#define SW_DIAL_LN   44
#define SW_LINE2     66
#define SW_NAME_Y    72
#define SW_LINE3    108
#define SW_VU_BOT   162
#define SW_INFO_Y   166
#define SW_LINE4    178
#define SW_TCK_Y    182
#define SW_LINE5    201
#define SW_BTN_Y    206

// ─── Tuning dial geometry ────────────────────────────────────────────────────
#define DIAL_X1      20
#define DIAL_X2     300

// ─── Touch footer detection ──────────────────────────────────────────────────
#define TOUCH_FOOTER_Y  202   // y threshold — anything below is a footer tap
#define TOUCH_DEBOUNCE  250   // ms between accepted taps
#define ZONE_W          107   // screen width / 3 ≈ 107 px per zone

// ─── I2S pins → NS4168 amp (Core2 onboard) ───────────────────────────────────
#define I2S_BCK   12
#define I2S_LRC    0
#define I2S_DOUT   2

// ─── SI4713 FM modulator ──────────────────────────────────────────────────────
#define SI4713_RST      13
#define FM_FREQ_MIN   8790   // 87.9 MHz
#define FM_FREQ_MAX  10790   // 107.9 MHz
#define FM_FREQ_STEP    20   // 0.2 MHz per step

// ─── Haptic helper (AXP192 LDO3 → vibration motor) ───────────────────────────
static void haptic(int ms = 40) {
    M5.Axp.SetLDOEnable(3, true);
    delay(ms);
    M5.Axp.SetLDOEnable(3, false);
}

// ─── Stations ────────────────────────────────────────────────────────────────
#define ns 12
// ROKiT Radio Network — OTR/classic streams (http://IP:PORT/stream)
String stations[ns] = {
    "http://149.255.60.195:8256/stream",   // 1940s Radio
    "http://149.255.60.193:8162/stream",   // American Comedy  ← Fibber McGee & Molly
    "http://149.255.60.194:8043/stream",   // American Classics
    "http://149.255.60.195:8027/stream",   // Jazz Central
    "http://149.255.60.195:8150/stream",   // Comedy Gold
    "http://149.255.60.195:8168/stream",   // Mystery Radio
    "http://149.255.60.193:8168/stream",   // Crime & Suspense
    "http://149.255.60.194:8039/stream",   // Crime Radio
    "http://149.255.60.195:8162/stream",   // Adventure Stories
    "http://149.255.60.195:8174/stream",   // Drama Radio
    "http://149.255.60.195:8180/stream",   // Nostalgia Lane
    "http://149.255.60.194:8110/stream",   // Science Fiction
};
String stationNames[ns] = {
    "1940s Radio",
    "American Comedy",
    "American Classics",
    "Jazz Central",
    "Comedy Gold",
    "Mystery Radio",
    "Crime & Suspense",
    "Crime Radio",
    "Adventure Stories",
    "Drama Radio",
    "Nostalgia Lane",
    "Science Fiction",
};

// ─── Objects ─────────────────────────────────────────────────────────────────
TFT_eSprite sprite2(&M5.Lcd);   // scrolling ticker (~10 KB)
// M5Core2 Speaker.cpp owns I2S_NUM_0 (installed at 44100 Hz on M5.begin()).
// Use I2S_NUM_1 so Audio gets a clean port with no clock conflict → correct pitch.
Audio       audio(false, 3, I2S_NUM_1);
// SI4713 uses ESP32 internal DAC on GPIO26 (Port B) via I2S_NUM_0 DAC mode.
// Created only when SI4713 is detected on I2C at boot.
Audio          *audioFM = nullptr;
Adafruit_Si4713 si4713(SI4713_RST);
Preferences     prefs;
WiFiMulti       wifiMulti;

// Audio runs on its own FreeRTOS task so the display loop never starves it
void audioTask(void *param) {
    while (true) {
        audio.loop();
        if (audioFM) audioFM->loop();
        vTaskDelay(1);
    }
}
TaskHandle_t audioTaskHandle = NULL;

// ─── State ───────────────────────────────────────────────────────────────────
String curStation   = "";
String songPlaying  = "";
long   bitrate      = 0;
bool   connected    = false;
int    songposition = -310;
float  voltage      = 4.20;
int    batLevel     = 0;
bool   canDraw      = false;
int    rssi         = 0;
int    chosen       = 0;
int    volume       = 5;    // 0-10 → audio.setVolume(volume * 2)
int    g[14]        = {0};

bool   inSettings   = false;
int    settingSel   = 0;    // 0=Volume 1=Bass 2=Treble [3=FM Freq if present]
int8_t settingBass  = 0;
int8_t settingTreble = 0;
bool   screenOn     = true; // toggled by long-press BtnC
bool   fmPresent    = false;
bool   fmMode       = false;   // false=speaker (NS4168), true=FM (SI4713 DAC)
uint16_t fmFreq     = 10110; // 101.1 MHz default

unsigned short grays[18];

// ─── Forward declarations ─────────────────────────────────────────────────────
void setupUI();
void drawSettings();

// ─── Switch between Speaker (NS4168) and FM (SI4713 DAC) output ─────────────
static void setAudioMode(bool useFM) {
    if (!fmPresent) return;
    fmMode = useFM;
    if (fmMode) {
        audio.stopSong();
        delay(100);
        audioFM->connecttohost(stations[chosen].c_str());
    } else {
        audioFM->stopSong();
        delay(100);
        audio.connecttohost(stations[chosen].c_str());
    }
    haptic(80);
    setupUI();   // redraw header so FM badge reflects new mode
    canDraw = true;
}

// ─── Guarded station change: stop cleanly, debounce 1.5 s ───────────────────
static unsigned long lastStationChange = 0;
static void changeStation(int newChosen) {
    if (millis() - lastStationChange < 1500) return;
    lastStationChange = millis();
    chosen = newChosen;
    // Only reconnect the active stream
    Audio &active = (fmMode && audioFM) ? *audioFM : audio;
    active.stopSong();
    delay(150);
    active.connecttohost(stations[chosen].c_str());
    if (fmPresent) {
        si4713.setRDSstation(stationNames[chosen].substring(0, 8).c_str());
        si4713.setRDSbuffer("Buffering...");
    }
    canDraw = true;
}

// ─── Persist FM frequency to NVS ────────────────────────────────────────────
static void saveFMFreq() {
    prefs.begin("radio", false);
    prefs.putUShort("fmfreq", fmFreq);
    prefs.end();
}

// ─── Settings action: increment currently selected parameter ─────────────────
static void settingsIncrement() {
    int maxSel = fmPresent ? 4 : 3;
    if (settingSel == 0) {
        volume++;
        if (volume > 10) volume = 0;
        audio.setVolume(volume * 2);
        if (audioFM) audioFM->setVolume(volume * 2);
    } else if (settingSel == 1) {
        settingBass++;
        if (settingBass > 6) settingBass = -6;
        audio.setTone(settingBass, 0, settingTreble);
    } else if (settingSel == 2) {
        settingTreble++;
        if (settingTreble > 6) settingTreble = -6;
        audio.setTone(settingBass, 0, settingTreble);
    } else if (settingSel == 3 && fmPresent) {
        fmFreq += FM_FREQ_STEP;
        if (fmFreq > FM_FREQ_MAX) fmFreq = FM_FREQ_MIN;
        si4713.tuneFM(fmFreq);
        saveFMFreq();
    }
    drawSettings();
}

// ─── Touch: return which footer zone (0/1/2) was tapped, or -1 ───────────────
static int footerZoneTapped(unsigned long &lastTouch, bool &prevTouch) {
    bool touchNow = M5.Touch.ispressed();
    bool edge     = touchNow && !prevTouch && (millis() - lastTouch > TOUCH_DEBOUNCE);
    prevTouch = touchNow;
    if (!edge) return -1;
    TouchPoint_t p = M5.Touch.getPressPoint();
    if (p.y < TOUCH_FOOTER_Y) return -1;
    lastTouch = millis();
    haptic();
    if (p.x < ZONE_W)       return 0;   // SET / BACK
    if (p.x < ZONE_W * 2)   return 1;   // STA / SEL
    return 2;                            // VOL / +
}

// ---------------------------------------------------------------------------
void setup() {
    M5.begin(true /*LCD*/, true /*SD*/, true /*Serial*/, false /*I2C*/);

    // M5Core2 has no built-in speaker driver in v0.1.9 — Audio takes I2S directly
    audio.setPinout(I2S_BCK, I2S_LRC, I2S_DOUT);
    audio.setVolume(volume * 2);

    int co = 214;
    for (int i = 0; i < 18; i++) {
        grays[i] = M5.Lcd.color565(co, co, co + 40);
        co -= 13;
    }

    sprite2.setColorDepth(16);
    sprite2.createSprite(310, 16);
    sprite2.setTextFont(2);
    sprite2.setTextColor(SW_AMBER, TFT_BLACK);

    rdLoadSettings();

    // Load persisted FM frequency
    prefs.begin("radio", true);
    fmFreq = prefs.getUShort("fmfreq", 10110);
    prefs.end();

    // Boot splash
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_GREEN);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.print("M5 Core2 Radio");
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.setCursor(4, 40);
    M5.Lcd.print("Hold BtnA for WiFi setup...");

    bool enterPortal = !rd_has_settings;
    unsigned long bootStart = millis();
    while (millis() - bootStart < 3000) {
        M5.update();
        if (M5.BtnA.isPressed()) { enterPortal = true; break; }
        delay(50);
    }

    if (enterPortal) {
        rdInitPortal();
        while (!portalDone) { rdRunPortal(); delay(1); }
        rdClosePortal();
    }

    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_GREEN);
    M5.Lcd.setCursor(2, 20);
    M5.Lcd.println("Connecting to WiFi...");

    WiFi.mode(WIFI_STA);
    wifiMulti.addAP(rd_wifi_ssid, rd_wifi_pass);
    wifiMulti.run();

    audio.connecttohost(stations[0].c_str());

    // ── SI4713 FM modulator probe with full I2C diagnostic ────────────────────
    // Explicit Wire init + manual RST toggle before library call
    Wire.begin(21, 22);
    delay(50);
    pinMode(SI4713_RST, OUTPUT);
    digitalWrite(SI4713_RST, HIGH); delay(10);
    digitalWrite(SI4713_RST, LOW);  delay(100);
    digitalWrite(SI4713_RST, HIGH); delay(150);

    // Raw I2C scan — results shown on screen AND serial
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_GREEN);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("I2C scan (SDA=GPIO21, SCL=GPIO22)");
    M5.Lcd.println("--------------------------------");
    Serial.println("\n=== I2C SCAN ===");
    bool anyFound = false;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            char buf[40];
            snprintf(buf, sizeof(buf), "  Device at 0x%02X", addr);
            M5.Lcd.setTextColor(addr == 0x63 ? TFT_YELLOW : TFT_CYAN);
            M5.Lcd.println(buf);
            Serial.println(buf);
            anyFound = true;
        }
    }
    if (!anyFound) {
        M5.Lcd.setTextColor(TFT_RED);
        M5.Lcd.println("  NO DEVICES FOUND");
        Serial.println("  NO DEVICES FOUND");
    }
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.println("\n0x63 = SI4713 (SEN high)");
    M5.Lcd.println("0x11 = SI4713 (SEN low)");
    M5.Lcd.println("\nWaiting 5s...");
    Serial.println("===============");
    delay(5000);

    if (si4713.begin()) {
        fmPresent = true;
        si4713.powerUp();
        si4713.setTXpower(115);
        si4713.tuneFM(fmFreq);
        si4713.beginRDS(0x1234);
        si4713.setRDSstation("M5 OTR");
        si4713.setRDSbuffer("M5Core2 OTR Radio");
        i2s_driver_uninstall(I2S_NUM_0);
        audioFM = new Audio(true, 3, I2S_NUM_0);
        audioFM->setVolume(volume * 2);
        // Do NOT connect here — starts in speaker mode; user toggles with long-press BtnA
        M5.Lcd.setTextColor(TFT_GREEN);
        M5.Lcd.println("SI4713 INIT OK");
    } else {
        M5.Lcd.setTextColor(TFT_RED);
        M5.Lcd.println("SI4713 begin() FAILED");
        Serial.println("SI4713 begin() failed");
    }
    delay(2000);

    // Pin audio to core 0 alongside WiFi; display loop stays on core 1
    xTaskCreatePinnedToCore(audioTask, "audioT", 8192, NULL, 2,
                            &audioTaskHandle, 0);

    setupUI();
    canDraw = true;
}

// ---------------------------------------------------------------------------
// setupUI — draw all static UI elements once. Called at boot and when
// returning from the settings overlay.
void setupUI() {
    M5.Lcd.startWrite();
    M5.Lcd.fillScreen(TFT_BLACK);

    // Header bar
    M5.Lcd.fillRect(0, 0, SCREEN_W, SW_LINE1, SW_HDR_BG);
    M5.Lcd.setTextFont(2);
    M5.Lcd.setTextColor(SW_AMBER, SW_HDR_BG);
    M5.Lcd.drawString("M5 SHORTWAVE", 6, 4);
    if (fmPresent) {
        // Green = FM mode active, grey = speaker mode active
        M5.Lcd.setTextColor(fmMode ? TFT_GREEN : grays[10], SW_HDR_BG);
        M5.Lcd.drawString("FM", 96, 4);
    }

    // Amber dividers
    const int divs[] = { SW_LINE1, SW_LINE2, SW_LINE3, SW_LINE4, SW_LINE5 };
    for (int i = 0; i < 5; i++)
        M5.Lcd.drawFastHLine(0, divs[i], SCREEN_W, SW_AMBER);

    // Tuning dial: line, tick marks, station numbers
    M5.Lcd.drawFastHLine(DIAL_X1, SW_DIAL_LN, DIAL_X2 - DIAL_X1 + 1, SW_AMBER_D);
    int dialSpacing = (DIAL_X2 - DIAL_X1) / (ns - 1);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(SW_AMBER_D, TFT_BLACK);
    for (int i = 0; i < ns; i++) {
        int tx = DIAL_X1 + i * dialSpacing;
        M5.Lcd.drawFastVLine(tx, 32, SW_DIAL_LN - 32, SW_AMBER_D);
        char lbl[3];
        snprintf(lbl, sizeof(lbl), "%d", i + 1);
        M5.Lcd.drawString(lbl, tx - 3, 25);
    }

    // VU empty bar slots
    for (int i = 0; i < 14; i++) {
        int bx = 10 + i * 21;
        for (int j = 0; j < 4; j++)
            M5.Lcd.fillRect(bx, SW_VU_BOT - 10 - j * 13, 19, 10, SW_SLOT_BG);
    }

    // Footer touch buttons
    const char *btnLbls[] = { "SET", "STA", "VOL" };
    const int   btnCx[]   = {  53,   160,   267 };
    M5.Lcd.setTextFont(2);
    for (int i = 0; i < 3; i++) {
        int bx = btnCx[i] - 30;
        M5.Lcd.fillRoundRect(bx, SW_BTN_Y, 60, 24, 5, SW_BTN_BG);
        M5.Lcd.drawRoundRect(bx, SW_BTN_Y, 60, 24, 5, SW_AMBER_D);
        M5.Lcd.setTextColor(SW_AMBER, SW_BTN_BG);
        M5.Lcd.drawCentreString(btnLbls[i], btnCx[i], SW_BTN_Y + 4, 2);
    }

    M5.Lcd.endWrite();
}

// ---------------------------------------------------------------------------
// drawDynamic — repaint only the regions that change. No full-screen wipe.
void drawDynamic() {
    M5.Lcd.startWrite();

    // Header dynamic zone
    M5.Lcd.fillRect(120, 2, 198, 20, SW_HDR_BG);
    M5.Lcd.fillCircle(127, 12, 4, connected ? TFT_GREEN : TFT_RED);

    int sigBars = 0;
    if (connected) {
        if      (rssi > -55) sigBars = 4;
        else if (rssi > -65) sigBars = 3;
        else if (rssi > -75) sigBars = 2;
        else                 sigBars = 1;
    }
    for (int i = 0; i < 4; i++) {
        int bh = 4 + i * 3, bx = 135 + i * 8;
        M5.Lcd.fillRect(bx, 22 - bh, 5, bh, (i < sigBars) ? TFT_GREEN : SW_GRN_DIM);
    }

    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(SW_AMBER_D, SW_HDR_BG);
    char staCur[8];
    snprintf(staCur, sizeof(staCur), "STA%d/%d", chosen + 1, ns);
    M5.Lcd.drawString(staCur, 166, 8);

    M5.Lcd.setTextColor(SW_AMBER, SW_HDR_BG);
    M5.Lcd.drawString(String(voltage, 2) + "V", 218, 8);

    // Battery icon
    M5.Lcd.drawRect(253, 6, 20, 12, SW_AMBER);
    M5.Lcd.fillRect(255, 8, 16, 8, TFT_BLACK);
    M5.Lcd.fillRect(255, 8, constrain(batLevel, 0, 13), 8,
                    batLevel > 4 ? TFT_GREEN : TFT_RED);
    M5.Lcd.fillRect(273, 9, 2, 4, SW_AMBER);

    // Tuning needle
    M5.Lcd.fillRect(0, SW_DIAL_LN + 1, SCREEN_W, 14, TFT_BLACK);
    int dialSpacing = (DIAL_X2 - DIAL_X1) / (ns - 1);
    int nx = DIAL_X1 + chosen * dialSpacing;
    M5.Lcd.fillTriangle(nx,     SW_DIAL_LN + 1,
                        nx - 6, SW_DIAL_LN + 13,
                        nx + 6, SW_DIAL_LN + 13, TFT_GREEN);

    // Station name
    M5.Lcd.fillRect(0, SW_LINE2 + 1, SCREEN_W, SW_LINE3 - SW_LINE2 - 1, TFT_BLACK);
    M5.Lcd.setTextFont(4);
    M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Lcd.drawCentreString(stationNames[chosen], 160, SW_NAME_Y, 4);

    // VU bars
    static const uint16_t vuOn[4] = { 0x07E0, 0x47E0, SW_AMBER, TFT_ORANGE };
    for (int i = 0; i < 14; i++) {
        g[i] = connected ? random(1, 5) : 0;
        int bx = 10 + i * 21;
        for (int j = 0; j < 4; j++) {
            int by = SW_VU_BOT - 10 - j * 13;
            M5.Lcd.fillRect(bx, by, 19, 10, j < g[i] ? vuOn[j] : SW_SLOT_BG);
        }
    }

    // Info row: bitrate + volume dots
    M5.Lcd.fillRect(0, SW_INFO_Y - 1, SCREEN_W, 12, TFT_BLACK);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(SW_AMBER, TFT_BLACK);
    M5.Lcd.drawString("BR:" + String(bitrate) + "k", 6, SW_INFO_Y);
    M5.Lcd.drawString("VOL:", 200, SW_INFO_Y);
    for (int i = 0; i < 10; i++)
        M5.Lcd.fillRect(224 + i * 7, SW_INFO_Y, 5, 7,
                        i < volume ? SW_AMBER : SW_DGREY);

    M5.Lcd.endWrite();
    canDraw = false;
}

void draw3() {
    songposition--;
    if (songposition < -310) songposition = 310;
    sprite2.fillSprite(TFT_BLACK);
    sprite2.drawString(">> " + songPlaying, songposition, 0);
    sprite2.pushSprite(5, SW_TCK_Y);
}

// ---------------------------------------------------------------------------
// drawSettings — full-screen sound settings overlay.
void drawSettings() {
    int numRows = fmPresent ? 4 : 3;
    int rowH    = fmPresent ? 42 : 55;
    int rowY0   = fmPresent ? 36 : 48;

    const char *labels[] = { "Volume", "Bass  ", "Treble", "FM MHz" };
    int mins[]  = { 0, -6, -6, (int)FM_FREQ_MIN };
    int maxs[]  = { 20,  6,  6, (int)FM_FREQ_MAX };

    M5.Lcd.startWrite();
    M5.Lcd.fillRect(0, 0, SCREEN_W, SCREEN_H, TFT_BLACK);

    M5.Lcd.setTextFont(2);
    M5.Lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
    M5.Lcd.drawString("== SOUND SETTINGS ==", 55, 10);
    M5.Lcd.drawFastHLine(0, 30, SCREEN_W, TFT_ORANGE);

    for (int i = 0; i < numRows; i++) {
        bool sel = (i == settingSel);
        int  y   = rowY0 + i * rowH;
        uint16_t bg = sel ? 0x1082 : TFT_BLACK;

        // value as string
        String valStr;
        int    valInt;
        if (i == 0) { valInt = volume * 2; valStr = String(valInt); }
        else if (i == 1) { valInt = settingBass;   valStr = String(valInt); }
        else if (i == 2) { valInt = settingTreble; valStr = String(valInt); }
        else             { valInt = fmFreq;         valStr = String(fmFreq / 100.0f, 1); }

        M5.Lcd.fillRect(20, y - 4, 280, rowH - 6, bg);
        M5.Lcd.setTextColor(sel ? TFT_GREEN : TFT_WHITE, bg);
        M5.Lcd.drawString(labels[i], 30, y);
        M5.Lcd.drawString(valStr, 230, y);

        int pos = map(valInt, mins[i], maxs[i], 0, 200);
        int by  = y + (fmPresent ? 16 : 20);
        int bh  = fmPresent ? 8 : 10;
        M5.Lcd.drawRect(30, by, 202, bh, sel ? TFT_GREEN : grays[12]);
        M5.Lcd.fillRect(31, by + 1, 200, bh - 2, TFT_BLACK);
        M5.Lcd.fillRect(31, by + 1, pos, bh - 2, sel ? TFT_GREEN : grays[8]);
    }

    // Footer buttons
    const char *sLbls[] = { "BACK", "SEL", "+" };
    const int   sCx[]   = {  53,    160,   267 };
    M5.Lcd.setTextFont(2);
    for (int i = 0; i < 3; i++) {
        int bx = sCx[i] - 30;
        M5.Lcd.fillRoundRect(bx, SW_BTN_Y, 60, 24, 5, SW_BTN_BG);
        M5.Lcd.drawRoundRect(bx, SW_BTN_Y, 60, 24, 5, SW_AMBER_D);
        M5.Lcd.setTextColor(SW_AMBER, SW_BTN_BG);
        M5.Lcd.drawCentreString(sLbls[i], sCx[i], SW_BTN_Y + 4, 2);
    }

    M5.Lcd.drawFastHLine(0, SCREEN_H - 34, SCREEN_W, TFT_ORANGE);
    M5.Lcd.setTextFont(1);
    M5.Lcd.setTextColor(grays[6], TFT_BLACK);
    M5.Lcd.drawString("BACK: exit    SEL: param    +: value", 26, SCREEN_H - 22);

    M5.Lcd.endWrite();
}

void measureBatt() {
    float v = M5.Axp.GetBatVoltage();
    voltage  = v;
    float pct = constrain((v - 3.0f) / 1.2f * 100.0f, 0.0f, 100.0f);
    batLevel  = (int)(pct / 100.0f * 13.0f);
}

// ---------------------------------------------------------------------------
void loop() {
    M5.update();

    static unsigned long lastRSSI  = 0;
    static unsigned long lastSlide = 0;
    static unsigned long lastDraw  = 0;
    static unsigned long lastTouch = 0;
    static bool          prevTouch = false;

    // ---- Sound settings mode ----
    if (inSettings) {
        if (M5.BtnA.wasPressed())                           { haptic(); inSettings = false; setupUI(); canDraw = true; }
        if (M5.BtnB.wasPressed())                           { haptic(); settingSel = (settingSel + 1) % (fmPresent ? 4 : 3); drawSettings(); }
        if (M5.BtnC.wasPressed())                           { haptic(); settingsIncrement(); }

        int z = footerZoneTapped(lastTouch, prevTouch);
        if      (z == 0)  { inSettings = false; setupUI(); canDraw = true; }
        else if (z == 1)  { settingSel = (settingSel + 1) % (fmPresent ? 4 : 3); drawSettings(); }
        else if (z == 2)  { settingsIncrement(); }

        vTaskDelay(5);
        return;
    }

    // ---- Normal radio mode ----
    if (millis() - lastRSSI > 240) {
        lastRSSI  = millis();
        rssi      = WiFi.RSSI();
        connected = (WiFi.status() == WL_CONNECTED);
        measureBatt();
        canDraw   = true;
        if (!connected) songPlaying = "WIFI NOT CONNECTED";
    }

    if (millis() - lastSlide > 30) {
        lastSlide = millis();
        draw3();
    }

    // Physical virtual buttons
    // BtnA: short-press = settings; long-press (1s) = toggle FM/Speaker (if FM present)
    static bool fmToggleHandled = false;
    if (M5.BtnA.pressedFor(1000) && !fmToggleHandled && fmPresent) {
        fmToggleHandled = true;
        setAudioMode(!fmMode);
    }
    if (M5.BtnA.wasReleased()) {
        if (!fmToggleHandled) { haptic(); inSettings = true; drawSettings(); }
        fmToggleHandled = false;
    }
    if (M5.BtnB.wasPressed()) {
        haptic();
        changeStation((chosen + 1) % ns);
    }
    // BtnC: short-press = volume up; long-press (1s) = toggle screen on/off
    static bool screenToggleHandled = false;
    if (M5.BtnC.pressedFor(1000) && !screenToggleHandled) {
        screenToggleHandled = true;
        screenOn = !screenOn;
        M5.Axp.SetDCVoltage(2, screenOn ? 3300 : 2500);
        if (screenOn) canDraw = true;
    }
    if (M5.BtnC.wasReleased()) {
        if (!screenToggleHandled) {
            haptic();
            volume = (volume >= 10) ? 0 : volume + 1;
            audio.setVolume(volume * 2);
            canDraw = true;
        }
        screenToggleHandled = false;
    }

    // Touch footer zones
    int z = footerZoneTapped(lastTouch, prevTouch);
    if (z == 0) { inSettings = true; drawSettings(); }
    else if (z == 1) {
        changeStation((chosen + 1) % ns);
    }
    else if (z == 2) {
        volume = (volume >= 10) ? 0 : volume + 1;
        audio.setVolume(volume * 2);
        canDraw = true;
    }

    if (canDraw && millis() - lastDraw > 800) {
        lastDraw = millis();
        drawDynamic();
    }

    vTaskDelay(5);
}

// ---------------------------------------------------------------------------
void audio_info(const char *info)            { Serial.print("info        "); Serial.println(info); }
void audio_id3data(const char *info)         { Serial.print("id3data     "); Serial.println(info); }
void audio_showstation(const char *info)     { curStation = info;   canDraw = true; }
void audio_showstreamtitle(const char *info) {
    songPlaying = info;
    canDraw = true;
    if (fmPresent && info && strlen(info) > 0) {
        si4713.setRDSbuffer(info);
    }
}
void audio_bitrate(const char *info)         { bitrate = String(info).toInt() / 1000; }
