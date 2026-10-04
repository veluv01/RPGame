#pragma once

// Pico / Pico 2 reference wiring. Set global -DRPGAME_* flags for a different PCB.
// GPIO numbers, rather than physical Pico header-pin numbers.
#if defined(ARDUINO_ARCH_RP2040)
#include <Arduino.h> // The core uses this architecture name for both chips.
#endif

#if defined(PICO_RP2350) && PICO_RP2350
#define RPGAME_CHIP_NAME "RP2350"
#elif defined(CHSIM)
#define RPGAME_CHIP_NAME "HOST"
#else
#define RPGAME_CHIP_NAME "RP2040"
#endif
#if defined(__riscv)
#define RPGAME_CPU_NAME "RISC-V"
#else
#define RPGAME_CPU_NAME "ARM"
#endif
#define RPGAME_PLATFORM_LABEL RPGAME_CHIP_NAME " / " RPGAME_CPU_NAME

// Arduino.h loads SDK headers before the variant overrides PICO_RP2350A.
// Read the final board selection here; an earlier NUM_BANK0_GPIOS may be 48
// even on a 30-GPIO RP2350A board such as Pico 2.
#if defined(PICO_RP2350) && PICO_RP2350 && !PICO_RP2350A
#define RPGAME_GPIO_COUNT 48
#else
#define RPGAME_GPIO_COUNT 30 // RP2040, RP2350A and portable drawing simulator.
#endif
#ifndef RPGAME_SPI_BUS
#define RPGAME_SPI_BUS 0
#endif
#ifndef RPGAME_SPI_SCK
#define RPGAME_SPI_SCK 2
#endif
#ifndef RPGAME_SPI_MOSI
#define RPGAME_SPI_MOSI 3
#endif
#ifndef RPGAME_SPI_MISO
#define RPGAME_SPI_MISO 4
#endif
// The PiZero's card socket is wired to SPI1, while the LCD stays on SPI0.
// Other boards default to the original shared bus. Flags apply to every
// library translation unit; the RPGame PiZero alias selects this profile.
#ifndef RPGAME_PIZERO_ONBOARD_SD
#if defined(ARDUINO_RPGAME_RP2350_PIZERO)
#define RPGAME_PIZERO_ONBOARD_SD 1
#else
#define RPGAME_PIZERO_ONBOARD_SD 0
#endif
#endif
#ifndef RPGAME_SD_SPI_BUS
#if RPGAME_PIZERO_ONBOARD_SD
#define RPGAME_SD_SPI_BUS 1
#else
#define RPGAME_SD_SPI_BUS RPGAME_SPI_BUS
#endif
#endif
#ifndef RPGAME_SD_SPI_SCK
#if RPGAME_PIZERO_ONBOARD_SD
#define RPGAME_SD_SPI_SCK 30
#else
#define RPGAME_SD_SPI_SCK RPGAME_SPI_SCK
#endif
#endif
#ifndef RPGAME_SD_SPI_MOSI
#if RPGAME_PIZERO_ONBOARD_SD
#define RPGAME_SD_SPI_MOSI 31
#else
#define RPGAME_SD_SPI_MOSI RPGAME_SPI_MOSI
#endif
#endif
#ifndef RPGAME_SD_SPI_MISO
#if RPGAME_PIZERO_ONBOARD_SD
#define RPGAME_SD_SPI_MISO 40
#else
#define RPGAME_SD_SPI_MISO RPGAME_SPI_MISO
#endif
#endif
#define RPGAME_SHARED_SPI (RPGAME_SPI_BUS == RPGAME_SD_SPI_BUS)
#ifndef RPGAME_LCD_CS
#define RPGAME_LCD_CS 5
#endif
#ifndef RPGAME_LCD_DC
#define RPGAME_LCD_DC 6
#endif
#ifndef RPGAME_LCD_RST
#define RPGAME_LCD_RST 7
#endif
#ifndef RPGAME_SD_CS
#if RPGAME_PIZERO_ONBOARD_SD
#define RPGAME_SD_CS 43
#else
#define RPGAME_SD_CS 8
#endif
#endif
#ifndef RPGAME_BUZZER
#define RPGAME_BUZZER 9
#endif
#ifndef RPGAME_BTN_UP
#define RPGAME_BTN_UP 10
#endif
#ifndef RPGAME_BTN_DOWN
#define RPGAME_BTN_DOWN 11
#endif
#ifndef RPGAME_BTN_LEFT
#define RPGAME_BTN_LEFT 12
#endif
#ifndef RPGAME_BTN_RIGHT
#define RPGAME_BTN_RIGHT 13
#endif
#ifndef RPGAME_BTN_A
#define RPGAME_BTN_A 14
#endif
#ifndef RPGAME_BTN_B
#define RPGAME_BTN_B 15
#endif
#ifndef RPGAME_BTN_SELECT
#define RPGAME_BTN_SELECT 16
#endif
#ifndef RPGAME_BTN_START
#define RPGAME_BTN_START 17
#endif
#ifndef RPGAME_LED
#define RPGAME_LED 25
#endif
#ifndef RPGAME_LCD_MAX_HZ
#define RPGAME_LCD_MAX_HZ 24000000u
#endif
#ifndef RPGAME_SD_MAX_HZ
#define RPGAME_SD_MAX_HZ 24000000u
#endif

#define PIN_LCD_CS RPGAME_LCD_CS
#define PIN_LCD_DC RPGAME_LCD_DC
#define PIN_LCD_RST RPGAME_LCD_RST
#define PIN_SD_CS RPGAME_SD_CS
#define PIN_BUZZER RPGAME_BUZZER
#define PIN_BTN_UP RPGAME_BTN_UP
#define PIN_BTN_DOWN RPGAME_BTN_DOWN
#define PIN_BTN_LEFT RPGAME_BTN_LEFT
#define PIN_BTN_RIGHT RPGAME_BTN_RIGHT
#define PIN_BTN_A RPGAME_BTN_A
#define PIN_BTN_B RPGAME_BTN_B
#define PIN_BTN_SELECT RPGAME_BTN_SELECT
#define PIN_BTN_START RPGAME_BTN_START

static_assert(RPGAME_SPI_BUS == 0 || RPGAME_SPI_BUS == 1, "Select SPI0 or SPI1");
static_assert(RPGAME_SPI_SCK >= 0 && RPGAME_SPI_SCK < RPGAME_GPIO_COUNT && RPGAME_SPI_SCK % 4 == 2 &&
              ((RPGAME_SPI_SCK / 8) % 2) == RPGAME_SPI_BUS, "Invalid hardware SPI SCK pin");
static_assert(RPGAME_SPI_MOSI >= 0 && RPGAME_SPI_MOSI < RPGAME_GPIO_COUNT && RPGAME_SPI_MOSI % 4 == 3 &&
              ((RPGAME_SPI_MOSI / 8) % 2) == RPGAME_SPI_BUS, "Invalid hardware SPI MOSI pin");
static_assert(RPGAME_SPI_MISO >= 0 && RPGAME_SPI_MISO < RPGAME_GPIO_COUNT && RPGAME_SPI_MISO % 4 == 0 &&
              ((RPGAME_SPI_MISO / 8) % 2) == RPGAME_SPI_BUS, "Invalid hardware SPI MISO pin");
static_assert(RPGAME_SD_SPI_BUS == 0 || RPGAME_SD_SPI_BUS == 1, "Select SD SPI0 or SPI1");
static_assert(RPGAME_SD_SPI_SCK >= 0 && RPGAME_SD_SPI_SCK < RPGAME_GPIO_COUNT && RPGAME_SD_SPI_SCK % 4 == 2 &&
              ((RPGAME_SD_SPI_SCK / 8) % 2) == RPGAME_SD_SPI_BUS, "Invalid SD hardware SPI SCK pin");
static_assert(RPGAME_SD_SPI_MOSI >= 0 && RPGAME_SD_SPI_MOSI < RPGAME_GPIO_COUNT && RPGAME_SD_SPI_MOSI % 4 == 3 &&
              ((RPGAME_SD_SPI_MOSI / 8) % 2) == RPGAME_SD_SPI_BUS, "Invalid SD hardware SPI MOSI pin");
static_assert(RPGAME_SD_SPI_MISO >= 0 && RPGAME_SD_SPI_MISO < RPGAME_GPIO_COUNT && RPGAME_SD_SPI_MISO % 4 == 0 &&
              ((RPGAME_SD_SPI_MISO / 8) % 2) == RPGAME_SD_SPI_BUS, "Invalid SD hardware SPI MISO pin");
static_assert(!RPGAME_SHARED_SPI || (RPGAME_SD_SPI_SCK == RPGAME_SPI_SCK &&
              RPGAME_SD_SPI_MOSI == RPGAME_SPI_MOSI && RPGAME_SD_SPI_MISO == RPGAME_SPI_MISO),
              "A shared SPI peripheral must use the same pin set for LCD and SD");

namespace rpgame_pins {
constexpr int spi[] = { RPGAME_SPI_SCK, RPGAME_SPI_MOSI, RPGAME_SPI_MISO,
    RPGAME_SD_SPI_SCK, RPGAME_SD_SPI_MOSI, RPGAME_SD_SPI_MISO };
constexpr int used[] = { RPGAME_LCD_CS, RPGAME_LCD_DC, RPGAME_LCD_RST, RPGAME_SD_CS, RPGAME_BUZZER,
    RPGAME_BTN_UP, RPGAME_BTN_DOWN, RPGAME_BTN_LEFT, RPGAME_BTN_RIGHT,
    RPGAME_BTN_A, RPGAME_BTN_B, RPGAME_BTN_SELECT, RPGAME_BTN_START, RPGAME_LED };
constexpr bool valid() {
    for (unsigned i = 0; i < sizeof used / sizeof *used; ++i) {
        if (used[i] < 0 || used[i] >= RPGAME_GPIO_COUNT) return false;
        for (unsigned j = 0; j < i; ++j) if (used[i] == used[j]) return false;
        for (int pin : spi) if (used[i] == pin) return false;
    }
    return true;
}
static_assert(valid(), "RPGame pins must be distinct GPIO numbers available on the selected chip package");
}
