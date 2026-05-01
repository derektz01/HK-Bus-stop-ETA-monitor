#pragma once
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// Routes every JsonDocument allocation to internal SRAM via heap_caps_malloc
// with MALLOC_CAP_INTERNAL. ArduinoJson's parse tree is dozens of small
// random-access writes; doing those in PSRAM (the default heap on ESP32-S3)
// contends with the LCD's RGB scanout DMA and shows up as horizontal drift
// bands. Forcing the tree into internal SRAM keeps the PSRAM bus quiet
// during fetch parses.
struct JsonInternalAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override {
        return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    void deallocate(void* p) override { heap_caps_free(p); }
    void* reallocate(void* p, size_t n) override {
        return heap_caps_realloc(p, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
};

inline ArduinoJson::Allocator* jsonInternalAllocator() {
    static JsonInternalAllocator inst;
    return &inst;
}
