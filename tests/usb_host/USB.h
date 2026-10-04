#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct mutex_t {};
inline void mutex_enter_blocking(mutex_t *) {}
inline void mutex_exit(mutex_t *) {}
struct USBClass {
    mutex_t mutex;
    void disconnect() {}
    void connect() {}
    uint8_t registerEndpointIn() { return 0x81; }
    uint8_t registerEndpointOut() { return 1; }
    static void simpleInterface(int, uint8_t *, int, void *) {}
    void registerInterface(int, void (*)(int, uint8_t *, int, void *), void *, size_t, int, uint32_t) {}
};
inline USBClass USB;
