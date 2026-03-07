#include "rollback.h"
#include "cfg.h"
#include "log.h"
#include "proc.h"
#include "utils.h"

int parse_pkg_file(FILE *fp, PkgInfo *arr, int max) {
    int count = 0;
    char line[256];
    while (fgets(line, (int)sizeof(line), fp) && count < max) {
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        if (sscanf(line, "%127s %127s", arr[count].name, arr[count].ver) == 2)
            count++;
    }
    return count;
}

static int cmp_pkg(const void *a, const void *b) {
    return strcmp(((const PkgInfo *)a)->name, ((const PkgInfo *)b)->name);
}

void sort_pkgs(PkgInfo *arr, int n) {
    qsort(arr, (size_t)n, sizeof(PkgInfo), cmp_pkg);
}

int find_pkg_in_cache(const char *name, const char *ver, char *out, size_t outsz) {
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

int do_rollback(const char *backup_dir) {
    LOGI("=== Rollback started from %s ===", backup_dir);
    printf("\n==========================================\n");
    printf("  SYSTEM ROLLBACK\n");
    printf("==========================================\n");
    printf("  Source: %s\n\n", backup_dir);

    char rblog_path[PATH_MAX];
    snprintf(rblog_path, sizeof(rblog_path), "%s/%s", backup_dir, ROLLBACK_LOG);
    FILE *rblog = fopen(rblog_path, "w");
    if (rblog) {
        time_t now = time(NULL);
        struct tm tmbuf; char ts[64] = "";
        if (localtime_r(&now, &tmbuf)) strftime(ts, sizeof(ts), "%F %T", &tmbuf);
        fprintf(rblog, "rollback_started=%s\nbackup_dir=%s\n\n", ts, backup_dir);
        fflush(rblog);
    }

    // Restore /boot — prefer the tar.gz archive, fall back to directory copy
    printf("[Rollback 1/4] Restoring /boot...\n");
    char boot_tar[PATH_MAX], boot_dir[PATH_MAX];
    snprintf(boot_tar, sizeof(boot_tar), "%s/%s", backup_dir, BOOT_BACKUP_NAME);
    snprintf(boot_dir, sizeof(boot_dir), "%s/boot",           backup_dir);

    if (access(boot_tar, F_OK) == 0) {
        char *argv[] = { TAR_BIN, "-xpzf", (char *)boot_tar, "-C", "/", NULL };
        if (run_mut(TAR_BIN, argv) == 0) {
            LOGI("/boot restored from archive %s", boot_tar);
            printf("  /boot restored from tar.gz\n");
            if (rblog) fprintf(rblog, "boot_restored=tar\n");
        } else {
            LOGW("Failed to extract %s", boot_tar);
            printf("  Warning: failed to restore /boot from archive\n");
        }
    } else if (access(boot_dir, F_OK) == 0) {
        char *argv[] = { CP_BIN, "-af", (char *)boot_dir, "/", NULL };
        if (run_mut(CP_BIN, argv) == 0) {
            LOGI("/boot restored from %s", boot_dir);
            printf("  /boot restored\n");
            if (rblog) fprintf(rblog, "boot_restored=cp\n");
        } else {
            LOGW("Failed to restore /boot from %s", boot_dir);
            printf("  Warning: failed to restore /boot\n");
        }
    } else {
        printf("  Warning: no /boot backup found, skipping\n");
        LOGW("No /boot backup in %s", backup_dir);
    }

    // Load the package state from before the update
    printf("[Rollback 2/4] Loading pre-update package list...\n");
    char before_path[PATH_MAX];
    snprintf(before_path, sizeof(before_path), "%s/%s", backup_dir, VERSIONS_BEFORE);
    FILE *fb = fopen(before_path, "r");
    if (!fb) {
        LOGE("Cannot open %s: %s", before_path, strerror(errno));
        if (rblog) { fprintf(rblog, "error=cannot_open_versions_before\n"); fclose(rblog); }
        return -1;
    }
    PkgInfo *before = calloc((size_t)MAX_PACKAGES, sizeof(PkgInfo));
    if (!before) { fclose(fb); if (rblog) fclose(rblog); return -1; }
    int nbefore = parse_pkg_file(fb, before, MAX_PACKAGES);
    fclose(fb);
    sort_pkgs(before, nbefore);
    printf("  Packages before update: %d\n", nbefore);

    // Get the current installed package list for diffing
    printf("[Rollback 3/4] Reading current package state...\n");
    char cur_path[PATH_MAX];
    snprintf(cur_path, sizeof(cur_path), "%s/current_versions.tmp", backup_dir);
    {
        char *argv[] = { PACMAN_BIN, "-Q", NULL };
        run_to_file(PACMAN_BIN, argv, cur_path);
    }
    FILE *fc = fopen(cur_path, "r");
    if (!fc) {
        LOGE("Cannot read current package list");
        free(before);
        if (rblog) fclose(rblog);
        return -1;
    }
    PkgInfo *current = calloc((size_t)MAX_PACKAGES, sizeof(PkgInfo));
    if (!current) { fclose(fc); free(before); if (rblog) fclose(rblog); return -1; }
    int ncurrent = parse_pkg_file(fc, current, MAX_PACKAGES);
    fclose(fc);
    unlink(cur_path);
    sort_pkgs(current, ncurrent);

    // Build the downgrade and removal argument lists
    printf("[Rollback 4/4] Applying package rollback...\n");

    char *downgrade_argv[MAX_ROLLBACK_PKGS + 4];
    int  ndowngrade = 0;
    downgrade_argv[ndowngrade++] = PACMAN_BIN;
    downgrade_argv[ndowngrade++] = "-U";
    downgrade_argv[ndowngrade++] = "--noconfirm";

    char *remove_argv[MAX_ROLLBACK_PKGS + 4];
    int  nremove = 0;
    remove_argv[nremove++] = PACMAN_BIN;
    remove_argv[nremove++] = "-Rns";
    remove_argv[nremove++] = "--noconfirm";

    int missing_cache = 0;
    char **pkgpaths = calloc((size_t)MAX_ROLLBACK_PKGS, sizeof(char *));
    if (!pkgpaths) { free(before); free(current); if (rblog) fclose(rblog); return -1; }

    for (int i = 0; i < nbefore && (ndowngrade - 3) < MAX_ROLLBACK_PKGS; i++) {
        PkgInfo key;
        strncpy(key.name, before[i].name, sizeof(key.name) - 1);
        key.name[sizeof(key.name) - 1] = '\0';

        PkgInfo *found = bsearch(&key, current, (size_t)ncurrent, sizeof(PkgInfo), cmp_pkg);
        int need_install = (!found || strcmp(found->ver, before[i].ver) != 0);

        if (need_install) {
            char ppath[PATH_MAX];
            if (find_pkg_in_cache(before[i].name, before[i].ver, ppath, sizeof(ppath)) == 0) {
                int idx = ndowngrade - 3;
                pkgpaths[idx] = strdup(ppath);
                downgrade_argv[ndowngrade] = pkgpaths[idx];
                ndowngrade++;
                if (!found)
                    printf("  reinstall: %s %s\n", before[i].name, before[i].ver);
                else
                    printf("  downgrade: %-30s %s -> %s\n",
                           before[i].name, found->ver, before[i].ver);
                if (rblog) fprintf(rblog, "%s=%s-%s\n",
                                   found ? "downgrade" : "reinstall",
                                   before[i].name, before[i].ver);
            } else {
                printf("  skip (not in cache): %s %s\n", before[i].name, before[i].ver);
                LOGW("Not in cache: %s %s", before[i].name, before[i].ver);
                if (rblog) fprintf(rblog, "missing_cache=%s-%s\n", before[i].name, before[i].ver);
                missing_cache++;
            }
        }
    }

    // Packages installed during the update that did not exist before
    for (int i = 0; i < ncurrent && (nremove - 3) < MAX_ROLLBACK_PKGS; i++) {
        PkgInfo key;
        strncpy(key.name, current[i].name, sizeof(key.name) - 1);
        key.name[sizeof(key.name) - 1] = '\0';
        PkgInfo *found = bsearch(&key, before, (size_t)nbefore, sizeof(PkgInfo), cmp_pkg);
        if (!found) {
            remove_argv[nremove++] = current[i].name;
            printf("  remove (new package): %s %s\n", current[i].name, current[i].ver);
            if (rblog) fprintf(rblog, "remove=%s-%s\n", current[i].name, current[i].ver);
        }
    }

    int rc = 0;

    if (ndowngrade > 3) {
        downgrade_argv[ndowngrade] = NULL;
        printf("\n  Running pacman -U (%d package(s))...\n", ndowngrade - 3);
        LOGI("pacman -U: %d package(s)", ndowngrade - 3);
        if (run_mut(PACMAN_BIN, downgrade_argv) != 0) {
            LOGE("pacman -U failed");
            if (rblog) fprintf(rblog, "downgrade_status=FAILED\n");
            rc = -1;
        } else {
            if (rblog) fprintf(rblog, "downgrade_status=OK\n");
        }
    }

    if (nremove > 3) {
        remove_argv[nremove] = NULL;
        printf("\n  Running pacman -Rns (%d package(s))...\n", nremove - 3);
        LOGI("pacman -Rns: %d package(s)", nremove - 3);
        if (run_mut(PACMAN_BIN, remove_argv) != 0) {
            LOGE("pacman -Rns failed");
            if (rblog) fprintf(rblog, "remove_status=FAILED\n");
            rc = -1;
        } else {
            if (rblog) fprintf(rblog, "remove_status=OK\n");
        }
    }

    for (int i = 0; i < ndowngrade - 3; i++) free(pkgpaths[i]);
    free(pkgpaths);
    free(before);
    free(current);

    if (missing_cache > 0)
        printf("\n  Warning: %d package(s) were not in cache.\n"
               "  See %s for manual recovery steps.\n",
               missing_cache, rblog_path);

    if (rc == 0) printf("\n  Rollback complete.\n");
    else         printf("\n  Rollback finished with errors. See %s\n", rblog_path);

    LOGI("=== Rollback finished (rc=%d) ===", rc);
    if (rblog) {
        fprintf(rblog, "rollback_rc=%d\nmissing_cache=%d\n", rc, missing_cache);
        fclose(rblog);
    }
    return rc;
}

int rollback_from_last(void) {
    char last[PATH_MAX];
    if (find_last_backup(last, sizeof(last)) != 0) {
        LOGE("No backups available to roll back from");
        return -1;
    }
    LOGI("Rolling back from: %s", last);
    printf("\nRolling back from: %s\n", last);
    return do_rollback(last);
}

void write_recovery_guide(const char *backup_dir, FsType fstype, const char *snap_name) {
    char path[PATH_MAX];
    int pn = snprintf(path, sizeof(path), "%s/%s", backup_dir, RECOVERY_FILE);
    if (pn < 0 || (size_t)pn >= sizeof(path)) return;

    FILE *f = fopen(path, "w");
    if (!f) { LOGW("Cannot create recovery.txt: %s", strerror(errno)); return; }

    time_t now = time(NULL);
    struct tm tmbuf; char ts[64] = "unknown";
    if (localtime_r(&now, &tmbuf)) strftime(ts, sizeof(ts), "%F %T", &tmbuf);

    fprintf(f,
        "=======================================================================\n"
        " RECOVERY GUIDE — %s v%s\n"
        " Generated: %s\n"
        " Backup:    %s\n"
        "=======================================================================\n\n",
        PROG_NAME, NS_VERSION, ts, backup_dir);

    fprintf(f,
        "-----------------------------------------------------------------------\n"
        "1. RESTORE /boot\n"
        "-----------------------------------------------------------------------\n"
        "# If the system does not boot, use a live USB:\n\n"
        "  mount /dev/sdXY /mnt\n"
        "  mount /dev/sdXZ /mnt/boot      # if /boot is a separate partition\n\n"
        "  # If boot was compressed (tar.gz):\n"
        "  tar -xpzf %s/%s -C /mnt\n\n"
        "  # If boot was copied as a directory:\n"
        "  cp -a %s/boot/* /mnt/boot/\n\n",
        backup_dir, BOOT_BACKUP_NAME, backup_dir);

    fprintf(f,
        "-----------------------------------------------------------------------\n"
        "2. RESTORE /etc\n"
        "-----------------------------------------------------------------------\n"
        "  # Full restore (overwrites all configs):\n"
        "  tar -xpzf %s/%s -C /\n\n"
        "  # Single file:\n"
        "  tar -xpzf %s/%s -C / etc/filename\n\n",
        backup_dir, ETC_BACKUP_NAME,
        backup_dir, ETC_BACKUP_NAME);

    fprintf(f,
        "-----------------------------------------------------------------------\n"
        "3. RESTORE PACMAN DATABASE\n"
        "-----------------------------------------------------------------------\n"
        "  cp -a %s/%s/* /var/lib/pacman/local/\n\n",
        backup_dir, PACMAN_DB_BACKUP);

    fprintf(f,
        "-----------------------------------------------------------------------\n"
        "4. AUTO ROLLBACK (recommended)\n"
        "-----------------------------------------------------------------------\n"
        "  narch-rollback --last\n"
        "  # or:\n"
        "  narchsafe rollback --last\n\n");

    fprintf(f,
        "-----------------------------------------------------------------------\n"
        "5. MANUAL PACKAGE ROLLBACK\n"
        "-----------------------------------------------------------------------\n"
        "  pacman -U %s/<pkgname>-<ver>-<arch>.pkg.tar.zst\n\n"
        "  # Pre-update package versions:\n"
        "  cat %s/%s\n\n"
        "  # Diff (shows what changed):\n"
        "  diff %s/%s %s/%s\n\n",
        g_cfg.pacman_cache,
        backup_dir, VERSIONS_BEFORE,
        backup_dir, VERSIONS_BEFORE,
        backup_dir, VERSIONS_AFTER);

    if (fstype == FS_BTRFS && snap_name && snap_name[0]) {
        fprintf(f,
            "-----------------------------------------------------------------------\n"
            "6. BTRFS SNAPSHOT ROLLBACK\n"
            "-----------------------------------------------------------------------\n"
            "  Snapshot: %s\n\n"
            "  # From a live USB:\n"
            "  mount /dev/sdX /mnt\n"
            "  btrfs subvolume delete /mnt/@\n"
            "  btrfs subvolume snapshot %s /mnt/@\n"
            "  reboot\n\n",
            snap_name, snap_name);
    } else if (fstype == FS_ZFS && snap_name && snap_name[0]) {
        fprintf(f,
            "-----------------------------------------------------------------------\n"
            "6. ZFS SNAPSHOT ROLLBACK\n"
            "-----------------------------------------------------------------------\n"
            "  Snapshot: %s\n\n"
            "  zfs rollback -r %s\n"
            "  reboot\n\n",
            snap_name, snap_name);
    }

    fprintf(f,
        "-----------------------------------------------------------------------\n"
        "7. EMERGENCY RECOVERY (live USB)\n"
        "-----------------------------------------------------------------------\n"
        "  1. Boot Arch Linux live USB\n"
        "  2. mount /dev/sdXY /mnt && arch-chroot /mnt\n"
        "  3. pacman -Syu     # or downgrade from cache\n"
        "  4. mkinitcpio -P   # rebuild initramfs\n"
        "  5. grub-mkconfig -o /boot/grub/grub.cfg\n"
        "  6. exit && reboot\n\n"
        "=======================================================================\n"
        " Log:          %s\n"
        " Rollback log: %s/%s\n"
        "=======================================================================\n",
        g_cfg.log_file, backup_dir, ROLLBACK_LOG);

    fclose(f);
    LOGI("Recovery guide written: %s", path);
    printf("  Recovery guide: %s\n", path);
}
