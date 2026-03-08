#include "lock.h"
#include "cfg.h"
#include "log.h"

int lock_acquire(void) {
    int fd = open(g_cfg.lock_file, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        LOGE("Cannot open lock file %s: %s", g_cfg.lock_file, strerror(errno));
        return -1;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            char pidbuf[32] = {0};
            ssize_t r = read(fd, pidbuf, sizeof(pidbuf) - 1);
            if (r > 0) {
                pidbuf[r] = '\0';
                char *nl = strchr(pidbuf, '\n'); if (nl) *nl = '\0';
                LOGE("Another instance is already running (PID %s)", pidbuf);
            } else {
                LOGE("Another instance is already running");
            }
        } else {
            LOGE("flock: %s", strerror(errno));
        }
        close(fd);
        return -1;
    }

    if (ftruncate(fd, 0) != 0) {
        LOGE("ftruncate: %s", strerror(errno));
        close(fd);
        return -1;
    }

    char pidbuf[32];
    int n = snprintf(pidbuf, sizeof(pidbuf), "%lld\n", (long long)getpid());
    if (n > 0) {
        if (write(fd, pidbuf, (size_t)n) != (ssize_t)n) {
            LOGE("Failed to write PID: %s", strerror(errno));
            close(fd);
            return -1;
        }
        (void)fsync(fd);   /* ensure PID is visible to other instances */
    }

    return fd;
}

void lock_release(int fd) {
    if (fd < 0) return;
    flock(fd, LOCK_UN);
    close(fd);
    unlink(g_cfg.lock_file);
}
