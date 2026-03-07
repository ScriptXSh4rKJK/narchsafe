#pragma once

#define NS_VERSION   "1.0.0"
#define PROG_NAME    "narchsafe"

#define DEFAULT_CONF_FILE        "/etc/narchsafe.conf"
#define DEFAULT_BACKUP_BASE      "/var/lib/narchsafe/backups"
#define DEFAULT_PACMAN_CACHE     "/var/cache/pacman/pkg"
#define DEFAULT_LOG_FILE         "/var/log/narchsafe.log"
#define DEFAULT_LOCK_FILE        "/var/run/narchsafe.lock"

// Not user-configurable
#define PACMAN_LOCAL_DB   "/var/lib/pacman/local"
#define PACMAN_DB_LOCK    "/var/lib/pacman/db.lck"

// Files written inside every backup directory
#define SENTINEL_FILE    ".complete"
#define UPDATE_LOG_NAME  "update.log"
#define VERSIONS_BEFORE  "versions_before.txt"
#define VERSIONS_AFTER   "versions_after.txt"
#define RECOVERY_FILE    "recovery.txt"
#define ETC_BACKUP_NAME  "etc_backup.tar.gz"
#define BOOT_BACKUP_NAME "boot_backup.tar.gz"
#define PACMAN_DB_BACKUP "pacman_local_db"
#define ROLLBACK_LOG     "rollback.log"

#define BTRFS_SNAP_PREFIX  "/@narchsafe-"
#define ZFS_SNAP_PREFIX    "narchsafe-"

#define PACMAN_BIN      "/usr/bin/pacman"
#define CP_BIN          "/bin/cp"
#define RM_BIN          "/bin/rm"
#define MKDIR_BIN       "/bin/mkdir"
#define TAR_BIN         "/usr/bin/tar"
#define BTRFS_BIN       "/usr/bin/btrfs"
#define ZFS_BIN         "/usr/sbin/zfs"
#define SYSTEMCTL_BIN   "/usr/bin/systemctl"
#define UNAME_BIN       "/usr/bin/uname"
#define NOTIFY_SEND_BIN "/usr/bin/notify-send"
#define CURL_BIN        "/usr/bin/curl"

#define DEFAULT_MIN_FREE_MB        512
#define DEFAULT_BACKUP_KEEP_COUNT  5
#define DEFAULT_KERNEL_FRESH_SECS  300
#define DEFAULT_BOOT_COMPRESS      0
#define DEFAULT_AUTO_ROLLBACK      1
#define DEFAULT_DO_SNAPSHOT        1
#define DEFAULT_NOTIFY_LIBNOTIFY   0
#define DEFAULT_NOTIFY_TELEGRAM    0
#define DEFAULT_TELEGRAM_API_URL   "https://api.telegram.org"

#define MAX_PACKAGES      8192
#define MAX_ROLLBACK_PKGS 1024
