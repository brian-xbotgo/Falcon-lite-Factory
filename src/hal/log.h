#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum LogLevel { LOG_ALL = 0, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR };
static volatile enum LogLevel log_level = LOG_INFO;

static inline void set_log_level(enum LogLevel level) { log_level = level; }

#define log_err(fmt, args...)   do { if (log_level <= LOG_ERROR) fprintf(stderr, "[ERR] " fmt, ##args); } while(0)
#define log_warn(fmt, args...)  do { if (log_level <= LOG_WARN)  fprintf(stderr, "[WARN] " fmt, ##args); } while(0)
#define log_info(fmt, args...)  do { if (log_level <= LOG_INFO)  fprintf(stderr, "[INFO] " fmt, ##args); } while(0)
#define log_debug(fmt, args...) do { if (log_level <= LOG_DEBUG) fprintf(stderr, "[DBG] " fmt, ##args); } while(0)
#define log_all(fmt, args...)   do { if (log_level <= LOG_ALL)   fprintf(stderr, "[ALL] " fmt, ##args); } while(0)

#ifdef __cplusplus
}
#endif

#endif
