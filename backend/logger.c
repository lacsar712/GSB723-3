#include "logger.h"
#include <string.h>

void log_message(LogLevel level, const char *format, ...) {
  time_t now;
  time(&now);
  char time_buf[64];
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));

  const char *level_str;
  switch (level) {
    case LOG_INFO: level_str = "INFO"; break;
    case LOG_WARN: level_str = "WARN"; break;
    case LOG_ERROR: level_str = "ERROR"; break;
    default: level_str = "UNKNOWN"; break;
  }

  fprintf(stdout, "[%s] [%s] ", time_buf, level_str);

  va_list args;
  va_start(args, format);
  vfprintf(stdout, format, args);
  va_end(args);

  fprintf(stdout, "\n");
  fflush(stdout);
}
