#include "utils.h"
#include "cfg.h"
#include "log.h"
#include "proc.h"

unsigned long long free_bytes(const char *path) {
    struct statvfs st;
    if (statvfs(path, &st) != 0) return 0ULL;
    return (unsigned long long)st.f_bsize * (unsigned long long)st.f_bavail;
}

int make_backup_path(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm tmbuf;
    if (!localtime_r(&now, &tmbuf)) {
        LOGE("localtime_r: %s", strerror(errno));
        return -1;
    }
    int n = snprintf(buf, size, "%s/%04d-%02d-%02d_%02d-%02d-%02d",
                     g_cfg.backup_base,
                     tmbuf.tm_year + 1900, tmbuf.tm_mon + 1, tmbuf.tm_mday,
                     tmbuf.tm_hour, tmbuf.tm_min, tmbuf.tm_sec);
    if (n < 0 || (size_t)n >= size) { LOGE("Backup path too long"); return -1; }
    return 0;
}

void make_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm tmbuf;
    if (!localtime_r(&now, &tmbuf))
        snprintf(buf, size, "0000-00-00_00-00-00");
    else
        strftime(buf, size, "%Y-%m-%d_%H-%M-%S", &tmbuf);
}

int write_sentinel(const char *dir) {
    if (g_dry_run) {
        printf("  [DRY-RUN] would write sentinel: %s/%s\n", dir, SENTINEL_FILE);
        return 0;
    }
    char path[PATH_MAX];
    int pn = snprintf(path, sizeof(path), "%s/%s", dir, SENTINEL_FILE);
    if (pn < 0 || (size_t)pn >= sizeof(path)) return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) { LOGE("sentinel open: %s", strerror(errno)); return -1; }
    time_t now = time(NULL);
    struct tm tmbuf;
    char ts[64] = "unknown";
    if (localtime_r(&now, &tmbuf)) strftime(ts, sizeof(ts), "%F %T", &tmbuf);
    char content[256];
    int cn = snprintf(content, sizeof(content),
                      "backup_complete=true\ntimestamp=%s\nversion=%s\n",
                      ts, NS_VERSION);
    if (cn > 0 && write(fd, content, (size_t)cn) != (ssize_t)cn)
        LOGW("Partial sentinel write");
    fsync(fd);
    close(fd);
    return 0;
}

void cleanup_partial_backup(const char *dir) {
    if (!dir || dir[0] == '\0') return;
    char sentinel[PATH_MAX];
    int sn = snprintf(sentinel, sizeof(sentinel), "%s/%s", dir, SENTINEL_FILE);
    if (sn < 0 || (size_t)sn >= sizeof(sentinel)) return;
    // Only remove if the sentinel is absent — a complete backup is left alone
    if (access(sentinel, F_OK) == 0) return;
    LOGW("Removing incomplete backup: %s", dir);
    char *argv[] = { RM_BIN, "-rf", "--", (char *)dir, NULL };
    (void)run_mut(RM_BIN, argv);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

void cleanup_old_backups(void) {
    DIR *d = opendir(g_cfg.backup_base);
    if (!d) return;

    char **names = NULL;
    int count = 0, cap = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        // Backup dirs are named YYYY-MM-DD_HH-MM-SS, so they start with a digit
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
        // Handle filesystems that don't populate d_type
        if (ent->d_type != DT_DIR && ent->d_type != DT_UNKNOWN) continue;
        if (ent->d_type == DT_UNKNOWN) {
            char tmp[PATH_MAX];
            struct stat st;
            if (snprintf(tmp, sizeof(tmp), "%s/%s", g_cfg.backup_base, ent->d_name) >= (int)sizeof(tmp)) continue;
            if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        }
        if (count >= cap) {
            int nc = (cap == 0) ? 32 : cap * 2;
            char **tmp = realloc(names, (size_t)nc * sizeof(char *));
            if (!tmp) { LOGW("OOM in cleanup_old_backups"); break; }
            names = tmp; cap = nc;
        }
        names[count] = strdup(ent->d_name);
        if (names[count]) count++;
    }
    closedir(d);

    if (count <= g_cfg.keep_count) goto done;

    qsort(names, (size_t)count, sizeof(char *), cmp_str);

    int to_del = count - g_cfg.keep_count;
    LOGI("Pruning %d old backup(s)", to_del);
    for (int i = 0; i < to_del; i++) {
        char full[PATH_MAX];
        int n = snprintf(full, sizeof(full), "%s/%s", g_cfg.backup_base, names[i]);
        if (n < 0 || (size_t)n >= sizeof(full)) continue;
        LOGI("  removing: %s", full);
        char *argv[] = { RM_BIN, "-rf", "--", (char *)full, NULL };
        (void)run_mut(RM_BIN, argv);
    }

done:
    for (int i = 0; i < count; i++) free(names[i]);
    free(names);
}

int find_last_backup(char *buf, size_t bufsz) {
    DIR *d = opendir(g_cfg.backup_base);
    if (!d) {
        LOGE("Cannot open backup dir %s: %s", g_cfg.backup_base, strerror(errno));
        return -1;
    }

    char best[NAME_MAX + 1] = {0};
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
        if (ent->d_type != DT_DIR && ent->d_type != DT_UNKNOWN) continue;
        if (ent->d_type == DT_UNKNOWN) {
            char tmp[PATH_MAX];
            struct stat st;
            if (snprintf(tmp, sizeof(tmp), "%s/%s", g_cfg.backup_base, ent->d_name) >= (int)sizeof(tmp)) continue;
            if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        }

        // Only consider complete backups
        char sentinel[PATH_MAX];
        int sn = snprintf(sentinel, sizeof(sentinel), "%s/%s/%s",
                          g_cfg.backup_base, ent->d_name, SENTINEL_FILE);
        if (sn < 0 || (size_t)sn >= sizeof(sentinel)) continue;
        if (access(sentinel, F_OK) != 0) continue;

        if (strcmp(ent->d_name, best) > 0) {
            size_t dlen = strnlen(ent->d_name, sizeof(best) - 1);
            memset(best, 0, sizeof(best));
            memcpy(best, ent->d_name, dlen);
        }
    }
    closedir(d);

    if (best[0] == '\0') {
        LOGE("No complete backups found in %s", g_cfg.backup_base);
        return -1;
    }

    int n = snprintf(buf, bufsz, "%s/%s", g_cfg.backup_base, best);
    if (n < 0 || (size_t)n >= bufsz) return -1;
    return 0;
}
