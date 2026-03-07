#include "log.h"
#include "cfg.h"

void log_init(void) {
    openlog(PROG_NAME, LOG_PID | LOG_NDELAY, LOG_SYSLOG);
    int fd = open(g_cfg.log_file, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd >= 0)
        g_logfile = fdopen(fd, "a");
}

void log_close(void) {
    if (g_logfile) { fclose(g_logfile); g_logfile = NULL; }
    closelog();
}

void log_msg(int prio, const char *prefix, const char *fmt, ...) {
    va_list ap;
    time_t now = time(NULL);
    struct tm tmbuf;
    char ts[32] = "0000-00-00 00:00:00";
    if (localtime_r(&now, &tmbuf))
        strftime(ts, sizeof(ts), "%F %T", &tmbuf);

    char msg[1024];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    syslog(prio, "%s", msg);

    if (g_logfile) {
        fprintf(g_logfile, "[%s] %s: %s\n", ts, prefix, msg);
        fflush(g_logfile);
    }

    // Warnings and errors go to stderr; info goes to stdout
    if (prio <= LOG_WARNING)
        fprintf(stderr, "%s: %s\n", prefix, msg);
    else
        printf("%s\n", msg);
}
