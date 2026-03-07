#include "update.h"
#include "cfg.h"
#include "log.h"
#include "proc.h"
#include "checks.h"
#include "notify.h"
#include "rollback.h"

FILE *open_update_log(const char *backup_dir) {
    if (g_dry_run) return NULL;
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s", backup_dir, UPDATE_LOG_NAME);
    if (n < 0 || (size_t)n >= sizeof(path)) return NULL;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) { LOGW("Cannot create update.log: %s", strerror(errno)); return NULL; }
    return fdopen(fd, "w");
}

int do_update_logged(FILE *logfp) {
    LOGI("Running pacman -Syu");
    char *argv[] = { PACMAN_BIN, "-Syu", "--noconfirm", NULL };
    int ret = run_tee_mut(PACMAN_BIN, argv, logfp);
    if (ret != 0) { LOGE("pacman -Syu exited with %d", ret); return -1; }
    return 0;
}

int backup_versions_after(const char *backup_dir) {
    char outfile[PATH_MAX];
    int on = snprintf(outfile, sizeof(outfile), "%s/%s", backup_dir, VERSIONS_AFTER);
    if (on < 0 || (size_t)on >= sizeof(outfile)) return -1;
    if (g_dry_run) {
        printf("  [DRY-RUN] would save: pacman -Q -> %s\n", outfile);
        return 0;
    }
    char *argv[] = { PACMAN_BIN, "-Q", NULL };
    if (run_to_file(PACMAN_BIN, argv, outfile) != 0) {
        LOGW("Failed to save versions_after.txt");
        return -1;
    }
    int fd = open(outfile, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) { fsync(fd); close(fd); }
    return 0;
}

static int try_rollback(const char *backup_dir, const char *reason) {
    printf("\n  Triggering auto-rollback: %s\n", reason);
    LOGI("Auto-rollback triggered: %s", reason);
    int rc = do_rollback(backup_dir);
    if (rc == 0) {
        notify_send(NOTIFY_ROLLBACK_OK, backup_dir);
        printf("\n  System rolled back to pre-update state.\n");
    } else {
        notify_send(NOTIFY_ROLLBACK_FAIL, "Manual recovery required");
        printf("\n  Rollback failed. Follow the manual recovery guide:\n"
               "    %s/%s\n", backup_dir, RECOVERY_FILE);
    }
    return rc;
}

int do_update_with_checks(const char *backup_dir, int auto_rollback) {
    printf("\n==========================================\n");
    printf("  RUNNING SYSTEM UPDATE%s\n", g_dry_run ? " [DRY-RUN]" : "");
    printf("==========================================\n\n");

    notify_send(NOTIFY_UPDATE_START, NULL);

    FILE *logfp  = open_update_log(backup_dir);
    int update_rc = do_update_logged(logfp);
    if (logfp) { fflush(logfp); fclose(logfp); }

    if (update_rc != 0) {
        LOGE("Update failed (exit %d)", update_rc);
        printf("\n  Update FAILED.\n");
        notify_send(NOTIFY_UPDATE_FAIL, auto_rollback ? "Rolling back..." : NULL);

        if (auto_rollback) {
            int rb = try_rollback(backup_dir, "pacman -Syu failed");
            return rb == 0 ? 1 : -1;
        }

        printf("  Auto-rollback is disabled.\n"
               "  To roll back manually:  narch-rollback --last\n");
        return -1;
    }

    LOGI("Update succeeded");
    printf("\n  Update complete.\n");

    backup_versions_after(backup_dir);

    // A degraded state after update is treated as a rollback trigger
    int sys_rc = check_systemctl_health();
    if (sys_rc < 0 && auto_rollback) {
        notify_send(NOTIFY_UPDATE_FAIL, "System in critical state after update");
        int rb = try_rollback(backup_dir, "system entered critical state post-update");
        return rb == 0 ? 1 : -1;
    }

    // If the kernel was updated but the image looks wrong, roll back
    int kern_rc = check_kernel_update(backup_dir);
    if (kern_rc != 0 && auto_rollback) {
        notify_send(NOTIFY_UPDATE_FAIL, "Kernel install check failed");
        int rb = try_rollback(backup_dir, "kernel install check failed");
        return rb == 0 ? 1 : -1;
    }

    notify_send(NOTIFY_UPDATE_OK, NULL);
    return 0;
}
