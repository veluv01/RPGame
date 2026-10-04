// RPGame bring-up: LCD, every button, buzzer, LED and a read-only SD check.
#include <RPGame.h>
#include <RPGameSD.h>

static RPGame game;
static bool sdOK;
static uint32_t rootEntries;

static void checkSD() {
    gfx_wait();
    sdOK = SD.begin(PIN_SD_CS);
    rootEntries = 0;
    if (sdOK) {
        File root = SD.open("/");
        if (!root) { sdOK = false; return; }
        while (true) {
            File entry = root.openNextFile();
            if (!entry) break;
            ++rootEntries;
            entry.close();
        }
        root.close();
    }
    Serial.printf("SD: %s, root entries: %lu\n", sdOK ? "OK" : "FAILED", (unsigned long)rootEntries);
}

void setup() {
    Serial.begin(115200);
    Serial.printf("LCD SPI%d; SD SPI%d SCK=%d MOSI=%d MISO=%d CS=%d\n",
        RPGAME_SPI_BUS, RPGAME_SD_SPI_BUS, RPGAME_SD_SPI_SCK,
        RPGAME_SD_SPI_MOSI, RPGAME_SD_SPI_MISO, PIN_SD_CS);
    game.boot();
    pinMode(RPGAME_LED, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    game.begin(GFX_DIV2, GFX_16BPP);
    const uint16_t colors[8] = {gfx_rgb(0, 0, 0), gfx_rgb(255, 255, 255),
        gfx_rgb(255, 0, 0), gfx_rgb(0, 255, 0), gfx_rgb(0, 0, 255),
        gfx_rgb(255, 255, 0), gfx_rgb(0, 255, 255), gfx_rgb(255, 0, 255)};
    game.setPalette(colors, 8);
    checkSD();
    game.setFrameRate(20);
}

void loop() {
    if (!game.nextFrame()) return;
    game.pollButtons();
    if (game.justPressed(A_BUTTON)) tone(PIN_BUZZER, 880, 120);
    if (game.justPressed(START_BUTTON)) checkSD();
    const uint8_t state = game.buttonsState();
    digitalWrite(RPGAME_LED, (millis() / 500) & 1);
    gfx_wait();
    gfx_clear(0);
    gfx_text(4, 3, "RPGAME HARDWARE", 1);
    for (int color = 1; color <= 7; ++color) gfx_fillRect((color - 1) * 18, 15, 18, 12, color);
    const char *names[8] = {"UP", "DOWN", "LEFT", "RIGHT", "A", "B", "SELECT", "START"};
    const uint8_t bits[8] = {UP_BUTTON, DOWN_BUTTON, LEFT_BUTTON, RIGHT_BUTTON,
        A_BUTTON, B_BUTTON, SELECT_BUTTON, START_BUTTON};
    for (int i = 0; i < 8; ++i) {
        const int x = (i & 1) ? 65 : 4;
        const int y = 33 + (i / 2) * 13;
        gfx_text(x, y, names[i], state & bits[i] ? 3 : 1);
    }
    char text[30];
    snprintf(text, sizeof text, "SD %s  %lu files", sdOK ? "OK" : "FAIL", (unsigned long)rootEntries);
    gfx_text(4, 88, text, sdOK ? 3 : 2);
    gfx_text(4, 101, "A: beep  START: SD", 1);
    gfx_text(4, 114, RPGAME_PLATFORM_LABEL, 1);
    gfx_rect(0, 0, 128, 128, 1);
    gfx_flushAsync();
    static uint8_t previous;
    if (state != previous) { Serial.printf("Buttons: 0x%02x\n", state); previous = state; }
}
