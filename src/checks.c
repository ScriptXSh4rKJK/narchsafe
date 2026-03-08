#include "checks.h"
#include "cfg.h"
#include "log.h"
#include "proc.h"

int check_pacman_lock(void) {
    int fd = open(PACMAN_DB_LOCK, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        close(fd);
        LOGE("Stale pacman lock detected: %s\n"
             "       A previous session may have ended abnormally.\n"
             "       Remove it and retry:\n"
             "         sudo rm %s",
             PACMAN_DB_LOCK, PACMAN_DB_LOCK);
        return -1;
    }
    return 0;
}

int show_pending_updates(void) {
    LOGI("Checking for available updates");
    printf("\nPending updates:\n");
    fflush(stdout);
    char *argv[] = { PACMAN_BIN, "-Qu", NULL };
    int ret = run(PACMAN_BIN, argv);
    if (ret == 1) { printf("Nothing to update.\n"); LOGI("System is up to date"); return 1; }
    if (ret != 0) { LOGE("pacman -Qu returned %d", ret); return -1; }
    return 0;
}

int check_systemctl_health(void) {
    if (access(SYSTEMCTL_BIN, X_OK) != 0) {
        LOGW("systemctl not found, skipping health check");
        return 0;
    }

    char status[64] = {0};
    char *argv[] = { SYSTEMCTL_BIN, "is-system-running", NULL };
    // Exit code is non-zero for anything other than "running"; we use the text output
    run_capture(SYSTEMCTL_BIN, argv, status, sizeof(status));

    printf("\n-- System health (systemctl) --\n");
    printf("  Status: %s\n", status[0] ? status : "(no response)");

    if (strcmp(status, "running") == 0) {
        printf("  All units running normally.\n");
        LOGI("systemctl is-system-running: running");
        return 0;
    }

    if (strcmp(status, "degraded") == 0) {
        printf("  System is degraded — some units have failed.\n"
               "  Run 'systemctl --failed' for details.\n");
        LOGW("systemctl is-system-running: degraded");
        return 1;
    }

    // maintenance / offline / unknown / stopping
    printf("  Critical system state: %s\n"
           "  Consider rolling back or investigating before rebooting.\n", status);
    LOGE("systemctl is-system-running: %s", status[0] ? status : "(empty)");
    return -1;
}

static int kernel_was_updated(const char *backup_dir) {
    char before_path[PATH_MAX], after_path[PATH_MAX];
    int bn = snprintf(before_path, sizeof(before_path), "%s/%s", backup_dir, VERSIONS_BEFORE);
    int an = snprintf(after_path,  sizeof(after_path),  "%s/%s", backup_dir, VERSIONS_AFTER);
    if (bn < 0 || (size_t)bn >= sizeof(before_path) ||
        an < 0 || (size_t)an >= sizeof(after_path))
        return 0;

    FILE *fb = fopen(before_path, "r");
    FILE *fa = fopen(after_path,  "r");
    if (!fb || !fa) {
        if (fb) fclose(fb);
        if (fa) fclose(fa);
        return 0;
    }

    typedef struct { char name[128]; char ver[128]; } KV;
    KV *after = calloc((size_t)MAX_PACKAGES, sizeof(KV));
    int nafter = 0;
    if (!after) { fclose(fb); fclose(fa); return 0; }

    char line[256];
    while (fgets(line, (int)sizeof(line), fa) && nafter < MAX_PACKAGES) {
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        if (sscanf(line, "%127s %127s", after[nafter].name, after[nafter].ver) == 2)
            nafter++;
    }
    fclose(fa);

    int updated = 0;
    while (fgets(line, (int)sizeof(line), fb) && !updated) {
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        char bname[128], bver[128];
        if (sscanf(line, "%127s %127s", bname, bver) != 2) continue;
        // Match linux, linux-lts, linux-zen, linux-hardened, etc.
        if (strncmp(bname, "linux", 5) != 0) continue;
        if (strstr(bname, "-headers") || strstr(bname, "-docs")) continue;
        for (int i = 0; i < nafter; i++) {
            if (strcmp(after[i].name, bname) == 0) {
                if (strcmp(after[i].ver, bver) != 0) updated = 1;
                break;
            }
        }
    }
    fclose(fb);
    free(after);
    return updated;
}

int check_kernel_update(const char *backup_dir) {
    if (!kernel_was_updated(backup_dir)) {
        LOGI("Kernel was not updated, skipping check");
        return 0;
    }

    printf("\n-- Kernel install check --\n");
    LOGI("Kernel was updated, verifying installation");

    // Prefer the default image; fall back to lts
    const char *vmlinuz = "/boot/vmlinuz-linux";
    struct stat st;
    if (stat(vmlinuz, &st) != 0) {
        vmlinuz = "/boot/vmlinuz-linux-lts";
        if (stat(vmlinuz, &st) != 0) {
            printf("  Kernel image not found at /boot/vmlinuz-linux\n");
            LOGE("Kernel image missing after update");
            return -1;
        }
    }

    time_t now = time(NULL);
    double age = difftime(now, st.st_mtime);
    if (age > g_cfg.kernel_fresh_secs) {
        printf("  %s last modified %.0f seconds ago (expected < %d)\n",
               vmlinuz, age, g_cfg.kernel_fresh_secs);
        LOGE("Kernel package updated but vmlinuz mtime unchanged (%.0f sec old)", age);
        return -1;
    }
    printf("  %s updated %.0f second(s) ago.\n", vmlinuz, age);

    // Verify that the matching module directory exists
    char cap[128] = {0};
    char *uname_argv[] = { UNAME_BIN, "-r", NULL };
    run_capture(UNAME_BIN, uname_argv, cap, sizeof(cap));
    if (cap[0]) {
        char moddir[PATH_MAX];
        int mn = snprintf(moddir, sizeof(moddir), "/usr/lib/modules/%s", cap);
        if (mn > 0 && (size_t)mn < sizeof(moddir)) {
            if (access(moddir, F_OK) == 0) {
                printf("  Kernel modules found: %s\n", moddir);
                LOGI("Kernel modules: %s", moddir);
            } else {
                printf("  Warning: module directory not found: %s\n", moddir);
                LOGW("Module directory missing: %s", moddir);
            }
        }
    }

    LOGI("Kernel install check passed");
    return 0;
}
