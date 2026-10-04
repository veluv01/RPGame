#pragma once
#include <cstdint>
#include <cstddef>
using uint = unsigned;
using byte = uint8_t;
constexpr unsigned SS=17, MOSI=19, MISO=16, SCK=18;
uint32_t millis();
uint32_t micros();
