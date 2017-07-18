#ifndef SYSLOG_H
#define SYSLOG_H

#define LOG_PID     0x01

#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

#define LOG_DAEMON  (3<<3)

void openlog(const char *ident, int logopt, int facility);
void syslog(int priority, const char *fmt, ...);

#endif /* SYSLOG_H */
