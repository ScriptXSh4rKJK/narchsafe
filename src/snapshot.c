#include "snapshot.h"
#include "log.h"
#include "proc.h"

FsType detect_root_fs(void) {
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) return FS_OTHER;
    char line[1024];   /* increased: device paths can be long */
    FsType result = FS_OTHER;
    while (fgets(line, (int)sizeof(line), fp)) {
        char dev[512], mnt[256], fstype[64];
        if (sscanf(line, "%511s %255s %63s", dev, mnt, fstype) != 3) continue;
        if (strcmp(mnt, "/") != 0) continue;
        if (strcmp(fstype, "btrfs") == 0) { result = FS_BTRFS; break; }
        if (strcmp(fstype, "zfs")   == 0) { result = FS_ZFS;   break; }
    }
    fclose(fp);
    return result;
}

int get_zfs_root_dataset(char *buf, size_t size) {
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) return -1;
    char line[1024];
    int found = 0;
    while (fgets(line, (int)sizeof(line), fp)) {
        char dev[512], mnt[256], fstype[64];
        if (sscanf(line, "%511s %255s %63s", dev, mnt, fstype) != 3) continue;
        if (strcmp(mnt, "/") != 0 || strcmp(fstype, "zfs") != 0) continue;
        int n = snprintf(buf, size, "%s", dev);
        if (n > 0 && (size_t)n < size) found = 1;
        break;
    }
    fclose(fp);
    return found ? 0 : -1;
}

int btrfs_snapshot_create(const char *ts) {
    if (access(BTRFS_BIN, X_OK) != 0) {
        LOGW("btrfs not found at %s, skipping snapshot", BTRFS_BIN);
        return -1;
    }

    int n = snprintf(g_btrfs_snap, sizeof(g_btrfs_snap), BTRFS_SNAP_PREFIX "%s", ts);
    if (n < 0 || (size_t)n >= sizeof(g_btrfs_snap)) return -1;

    LOGI("Creating Btrfs snapshot: %s", g_btrfs_snap);
    printf("  Btrfs snapshot: %s\n", g_btrfs_snap);

    char *argv[] = {
        BTRFS_BIN, "subvolume", "snapshot", "-r",
        "/", (char *)g_btrfs_snap, NULL
    };
    int ret = run_mut(BTRFS_BIN, argv);
    if (ret != 0) {
        LOGW("Btrfs snapshot failed (exit %d), proceeding without it", ret);
        g_btrfs_snap[0] = '\0';
        return -1;
    }

    LOGI("Btrfs snapshot created: %s", g_btrfs_snap);
    return 0;
}

int zfs_snapshot_create(const char *ts) {
    if (access(ZFS_BIN, X_OK) != 0) {
        LOGW("zfs not found at %s, skipping snapshot", ZFS_BIN);
        return -1;
    }

    char dataset[256];
    if (get_zfs_root_dataset(dataset, sizeof(dataset)) != 0) {
        LOGW("Cannot determine ZFS dataset for /");
        return -1;
    }

    int n = snprintf(g_zfs_snap, sizeof(g_zfs_snap),
                     "%s@" ZFS_SNAP_PREFIX "%s", dataset, ts);
    if (n < 0 || (size_t)n >= sizeof(g_zfs_snap)) return -1;

    LOGI("Creating ZFS snapshot: %s", g_zfs_snap);
    printf("  ZFS snapshot: %s\n", g_zfs_snap);

    char *argv[] = { ZFS_BIN, "snapshot", (char *)g_zfs_snap, NULL };
    int ret = run_mut(ZFS_BIN, argv);
    if (ret != 0) {
        LOGW("ZFS snapshot failed (exit %d), proceeding without it", ret);
        g_zfs_snap[0] = '\0';
        return -1;
    }

    LOGI("ZFS snapshot created: %s", g_zfs_snap);
    return 0;
}
