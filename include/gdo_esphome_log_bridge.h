#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Bridge functions so gdolib's C code can log into ESPHome's logger.
// Implemented in a C++ file so we can include esphome/core/log.h.
void gdolib_esphome_log_e(const char *tag, const char *fmt, ...);
void gdolib_esphome_log_w(const char *tag, const char *fmt, ...);
void gdolib_esphome_log_i(const char *tag, const char *fmt, ...);
void gdolib_esphome_log_d(const char *tag, const char *fmt, ...);
void gdolib_esphome_log_v(const char *tag, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

