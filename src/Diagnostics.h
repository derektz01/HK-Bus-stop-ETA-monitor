#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Print one line of internal-SRAM + PSRAM heap stats with the given tag.
// `largest_internal` is the number that actually decides TLS handshake
// success (mbedtls needs a contiguous ~16 KB allocation).
void Heap_Log(const char *tag);

#ifdef __cplusplus
}
#endif
