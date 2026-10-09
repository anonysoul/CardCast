#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

using TaskHandle_t = void*;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
constexpr int pdTRUE = 1;
constexpr int pdPASS = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
inline uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
inline void vTaskDelay(uint32_t) {}
inline void vTaskDelete(TaskHandle_t) {}
inline uint32_t ulTaskNotifyTake(int, uint32_t) { return 1; }
extern unsigned fakeNotifications;
inline void xTaskNotifyGive(TaskHandle_t) { ++fakeNotifications; }
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int,
                                 TaskHandle_t* handle, int) {
    *handle = reinterpret_cast<void*>(1);
    return pdPASS;
}
struct FakeMic {
    bool record(int16_t*, size_t, uint32_t) { return true; }
    int isRecording() const { return 2; }
    bool begin() { return true; }
    void end() {}
};
struct FakeCardputer { FakeMic Mic; };
extern FakeCardputer M5Cardputer;
extern unsigned fakeSerialPrints;
struct FakeSerial {
    template<class... Args> void printf(const char*, Args...) { ++fakeSerialPrints; }
    void println(const char*) {}
};
extern FakeSerial Serial;
uint32_t millis();
uint32_t micros();
