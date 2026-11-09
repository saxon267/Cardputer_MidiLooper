#pragma once
// Minimal host-side Arduino shim for the pure loop engine tests.
#include <stdint.h>
#include <stddef.h>

extern unsigned long g_test_ms;
extern unsigned long g_test_us;

inline unsigned long millis() { return g_test_ms; }
inline unsigned long micros() { return g_test_us; }
inline void delay(unsigned long) {}

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) do { (void)(m); } while (0)
#define portEXIT_CRITICAL(m)  do { (void)(m); } while (0)
