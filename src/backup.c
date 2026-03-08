#include "backup.h"
#include "cfg.h"
#include "log.h"
#include "proc.h"
#include "utils.h"

int create_backup_dir(const char *dir) {
    char *argv[] = { MKDIR_BIN, "-p", g_cfg.backup_base, NULL };
    if (run_mut(MKDIR_BIN, argv) != 0) {
        LOGE("Failed to create base dir: %s", g_cfg.backup_base);
        return -1;
    }
    if (!g_dry_run) {
        if (chmod(g_cfg.backup_base, 0700) != 0)
            LOGW("chmod(%s, 0700): %s", g_cfg.backup_base, strerror(errno));
    }
    if (g_dry_run) {
        printf("  [DRY-RUN] would mkdir: %s\n", dir);
        return 0;
    }
    if (mkdir(dir, 0700) != 0) {
        LOGE("mkdir(%s): %s", dir, strerror(errno));
        return -1;
    }
    return 0;
}

int backup_package_list(const char *dir) {
    char outfile[PATH_MAX];
    int on = snprintf(outfile, sizeof(outfile), "%s/packages.txt", dir);
    if (on < 0 || (size_t)on >= sizeof(outfile)) return -1;
    if (g_dry_run) {
        printf("  [DRY-RUN] would save: pacman -Qqe -> %s\n", outfile);
        return 0;
    }
    char *argv[] = { PACMAN_BIN, "-Qqe", NULL };
    if (run_to_file(PACMAN_BIN, argv, outfile) != 0) {
        LOGE("Failed to save package list");
        return -1;
    }
    int fd = open(outfile, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) { fsync(fd); close(fd); }
    return 0;
}

int backup_versions(const char *dir) {
    char outfile[PATH_MAX];
    int on = snprintf(outfile, sizeof(outfile), "%s/%s", dir, VERSIONS_BEFORE);
    if (on < 0 || (size_t)on >= sizeof(outfile)) return -1;
    if (g_dry_run) {
        printf("  [DRY-RUN] would save: pacman -Q -> %s\n", outfile);
        return 0;
    }
    char *argv[] = { PACMAN_BIN, "-Q", NULL };
    if (run_to_file(PACMAN_BIN, argv, outfile) != 0) {
        LOGE("Failed to save package versions");
        return -1;
    }
    int fd = open(outfile, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) { fsync(fd); close(fd); }
    return 0;
}

static int backup_boot_cp(const char *dir) {
    char boot_dest[PATH_MAX];
    int bn = snprintf(boot_dest, sizeof(boot_dest), "%s/boot", dir);
    if (bn < 0 || (size_t)bn >= sizeof(boot_dest)) return -1;
    char *argv[] = { CP_BIN, "-a", "/boot", (char *)boot_dest, NULL };
    if (run_mut(CP_BIN, argv) != 0) { LOGE("Failed to copy /boot"); return -1; }
    LOGI("/boot copied to %s/boot", dir);
    return 0;
}

static int backup_boot_tar(const char *dir) {
    if (access(TAR_BIN, X_OK) != 0) { LOGE("tar not found: %s", TAR_BIN); return -1; }
    char archive[PATH_MAX];
    int an = snprintf(archive, sizeof(archive), "%s/%s", dir, BOOT_BACKUP_NAME);
    if (an < 0 || (size_t)an >= sizeof(archive)) return -1;
    if (g_dry_run) {
        printf("  [DRY-RUN] would run: tar -cpzf %s /boot\n", archive);
        return 0;
    }
    char *argv[] = { TAR_BIN, "-cpzf", (char *)archive, "--warning=no-file-changed", "/boot", NULL };
    int ret = run(TAR_BIN, argv);
    if (ret != 0 && ret != 1) { LOGE("tar /boot returned %d", ret); return -1; }
    LOGI("/boot archived to %s", archive);
    return 0;
}

int backup_boot(const char *dir) {
    unsigned long long avail = free_bytes(g_cfg.backup_base);
    if (!g_dry_run && avail < g_cfg.min_free_bytes) {
        LOGE("Insufficient disk space: %.1f MB available, %.1f MB required",
             (double)avail                / (1024.0 * 1024.0),
             (double)g_cfg.min_free_bytes / (1024.0 * 1024.0));
        return -1;
    }
    return g_cfg.boot_compress ? backup_boot_tar(dir) : backup_boot_cp(dir);
}

int backup_etc(const char *dir) {
    if (access(TAR_BIN, X_OK) != 0) {
        LOGW("tar not found at %s, skipping /etc backup", TAR_BIN);
        return 0;
    }
    char archive[PATH_MAX];
    int an = snprintf(archive, sizeof(archive), "%s/%s", dir, ETC_BACKUP_NAME);
    if (an < 0 || (size_t)an >= sizeof(archive)) return -1;
    if (g_dry_run) {
        printf("  [DRY-RUN] would run: tar -cpzf %s /etc\n", archive);
        return 0;
    }
    char *argv[] = {
        TAR_BIN, "-cpzf", (char *)archive,
        "--exclude=/etc/mtab", "--exclude=/etc/.updated",
        "--warning=no-file-changed", "/etc", NULL
    };
    int ret = run(TAR_BIN, argv);
    if (ret != 0 && ret != 1) LOGW("tar /etc returned %d (non-fatal)", ret);
    else                       LOGI("/etc archived to %s", archive);
    return 0;
}

int backup_pacman_db(const char *dir) {
    char db_dest[PATH_MAX];
    int bn = snprintf(db_dest, sizeof(db_dest), "%s/%s", dir, PACMAN_DB_BACKUP);
    if (bn < 0 || (size_t)bn >= sizeof(db_dest)) return -1;
    char *argv[] = { CP_BIN, "-a", PACMAN_LOCAL_DB, (char *)db_dest, NULL };
    if (run_mut(CP_BIN, argv) != 0) {
        LOGW("Failed to copy pacman local DB, continuing");
        return 0;
    }
    LOGI("Pacman DB saved to %s", db_dest);
    return 0;
}

typedef struct { char name[128]; char ver[128]; } PkgPair;

static int find_in_cache(const char *name, const char *ver, char *out, size_t outsz) {
    char prefix[260];
    int pn = snprintf(prefix, sizeof(prefix), "%s-%s-", name, ver);
    if (pn < 0 || (size_t)pn >= sizeof(prefix)) return -1;
    size_t prelen = strlen(prefix);
    DIR *d = opendir(g_cfg.pacman_cache);
    if (!d) return -1;
    int found = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && !found) {
        if (strncmp(ent->d_name, prefix, prelen) != 0) continue;
        if (!strstr(ent->d_name, ".pkg.tar.")) continue;
        int n = snprintf(out, outsz, "%s/%s", g_cfg.pacman_cache, ent->d_name);
        if (n > 0 && (size_t)n < outsz) found = 1;
    }
    closedir(d);
    return found ? 0 : -1;
}

void check_cache_integrity(const char *backup_dir) {
    if (g_dry_run) { printf("  [DRY-RUN] would check cache integrity\n"); return; }
    char verfile[PATH_MAX];
    int vn = snprintf(verfile, sizeof(verfile), "%s/%s", backup_dir, VERSIONS_BEFORE);
    if (vn < 0 || (size_t)vn >= sizeof(verfile)) return;
    FILE *fp = fopen(verfile, "r");
    if (!fp) return;

    PkgPair *pkgs = calloc((size_t)MAX_PACKAGES, sizeof(PkgPair));
    if (!pkgs) { fclose(fp); return; }
    int npkgs = 0;
    char line[256];
    while (fgets(line, (int)sizeof(line), fp) && npkgs < MAX_PACKAGES) {
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        if (sscanf(line, "%127s %127s", pkgs[npkgs].name, pkgs[npkgs].ver) == 2)
            npkgs++;
    }
    fclose(fp);

    int missing = 0;
    char dummy[PATH_MAX];
    printf("\n-- Package cache check --\n");
    for (int i = 0; i < npkgs; i++) {
        if (find_in_cache(pkgs[i].name, pkgs[i].ver, dummy, sizeof(dummy)) != 0) {
            // Warn once with a header before listing the first missing package
            if (missing == 0)
                printf("Packages missing from cache (auto-rollback unavailable for these):\n");
            printf("  ! %-30s %s\n", pkgs[i].name, pkgs[i].ver);
            LOGW("Not in cache: %s %s", pkgs[i].name, pkgs[i].ver);
            missing++;
        }
    }
    if (missing == 0)
        printf("All %d packages present in cache.\n", npkgs);
    else
        printf("\nMissing: %d package(s). Manual rollback will require re-downloading.\n", missing);
    free(pkgs);
}

int do_full_backup(const char *backup_dir) {
    printf("\n[1/5] Saving package list...\n");
    if (backup_package_list(backup_dir) != 0) return -1;

    printf("[2/5] Saving package versions...\n");
    if (backup_versions(backup_dir) != 0) return -1;

    printf("[3/5] Backing up /boot (%s)...\n",
           g_cfg.boot_compress ? "tar.gz" : "cp -a");
    if (backup_boot(backup_dir) != 0) return -1;

    printf("[4/5] Archiving /etc...\n");
    backup_etc(backup_dir);

    printf("[5/5] Saving pacman database...\n");
    backup_pacman_db(backup_dir);

    check_cache_integrity(backup_dir);
    return 0;
}
