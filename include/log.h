#pragma once
#include "common.h"

void log_init(void);
void log_close(void);

__attribute__((format(printf, 3, 4)))
void log_msg(int prio, const char *prefix, const char *fmt, ...);

#define LOGI(...) log_msg(LOG_INFO,    "INFO",    __VA_ARGS__)
#define LOGW(...) log_msg(LOG_WARNING, "WARNING", __VA_ARGS__)
#define LOGE(...) log_msg(LOG_ERR,     "ERROR",   __VA_ARGS__)
