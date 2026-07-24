#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <time.h>
#include <stdarg.h>

typedef enum {
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR
} LogLevel;

void log_message(LogLevel level, const char *format, ...);

#endif
