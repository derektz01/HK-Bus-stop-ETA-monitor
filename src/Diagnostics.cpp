#include "Diagnostics.h"
#include <esp_heap_caps.h>
#include <stdio.h>

void Heap_Log(const char *tag)
{
    size_t free_int    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t min_int     = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    size_t free_psram  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    // Bytes (not KB) so the 16 384-byte mbedtls threshold is visible at a glance.
    printf("[HEAP] %-22s free_int=%6u largest_int=%6u min_int=%6u free_psram=%7u\n",
           tag,
           (unsigned)free_int,
           (unsigned)largest_int,
           (unsigned)min_int,
           (unsigned)free_psram);
}
