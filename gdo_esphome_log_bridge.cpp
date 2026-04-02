#include "gdo_esphome_log_bridge.h"

#include <cstdarg>

// We can't include "esphome/core/log.h" here because this file is built as a
// PlatformIO library source and ESPHome's include paths are not guaranteed.
// Instead, forward-declare the logger vprintf symbol that ESPHome provides.
namespace esphome {
// Levels match the ESPHome definitions:
// NONE=0, ERROR=1, WARN=2, INFO=3, CONFIG=4, DEBUG=5, VERBOSE=6, VERY_VERBOSE=7
inline constexpr int ESPHOME_LOG_LEVEL_ERROR = 1;
inline constexpr int ESPHOME_LOG_LEVEL_WARN = 2;
inline constexpr int ESPHOME_LOG_LEVEL_INFO = 3;
inline constexpr int ESPHOME_LOG_LEVEL_DEBUG = 5;

void esp_log_vprintf_(int level, const char *tag, int line, const char *format, va_list args);
} // namespace esphome

extern "C" {

void gdolib_esphome_log_v(const char *tag, const char *fmt, ...) {
    // NOTE: this variant isn't used directly; kept for completeness.
    // If you need it, call gdolib_esphome_log_* below which forward va_list correctly.
    va_list args;
    va_start(args, fmt);
    ::esphome::esp_log_vprintf_(esphome::ESPHOME_LOG_LEVEL_INFO, tag, 0, fmt, args);
    va_end(args);
}

static void gdolib_esphome_log_va(int level, const char *tag, const char *fmt, va_list args) {
    ::esphome::esp_log_vprintf_(level, tag, 0, fmt, args);
}

void gdolib_esphome_log_e(const char *tag, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gdolib_esphome_log_va(esphome::ESPHOME_LOG_LEVEL_ERROR, tag, fmt, args);
    va_end(args);
}

void gdolib_esphome_log_w(const char *tag, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gdolib_esphome_log_va(esphome::ESPHOME_LOG_LEVEL_WARN, tag, fmt, args);
    va_end(args);
}

void gdolib_esphome_log_i(const char *tag, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gdolib_esphome_log_va(esphome::ESPHOME_LOG_LEVEL_INFO, tag, fmt, args);
    va_end(args);
}

void gdolib_esphome_log_d(const char *tag, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    gdolib_esphome_log_va(esphome::ESPHOME_LOG_LEVEL_DEBUG, tag, fmt, args);
    va_end(args);
}

} // extern "C"

