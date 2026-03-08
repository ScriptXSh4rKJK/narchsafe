#include "common.h"
#include "cfg.h"
#include "log.h"
#include "lock.h"
#include "proc.h"
#include "utils.h"
#include "snapshot.h"
#include "backup.h"
#include "checks.h"
#include "update.h"
#include "notify.h"
#include "rollback.h"

volatile sig_atomic_t  g_interrupted          = 0;
char                   g_backup_dir[PATH_MAX]  = {0};
int                    g_lock_fd              = -1;
char                   g_btrfs_snap[PATH_MAX]  = {0};
char                   g_zfs_snap[512]         = {0};
FILE                  *g_logfile              = NULL;
int                    g_dry_run              = 0;

static void sig_handler(int sig) { (void)sig; g_interrupted = 1; }

static void signals_setup(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sig_handler;
    sa.sa_flags   = SA_RESTART;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sa.sa_handler = SIG_IGN;
    sa.sa_flags   = 0;
    sigaction(SIGPIPE, &sa, NULL);
}

static void do_cleanup(void) {
    if (g_lock_fd >= 0) { lock_release(g_lock_fd); g_lock_fd = -1; }
    log_close();
}

static void print_usage(const char *argv0) {
    printf(
        "Usage:\n"
        "  %s [OPTIONS]               Update system with backup\n"
        "  %s rollback --last         Roll back to the latest backup\n"
        "  %s rollback <DIR>          Roll back from a specific backup directory\n"
        "  narch-rollback --last      Alias for 'rollback --last'\n"
        "\n"
        "Options:\n"
        "  --config <FILE>   Config file path (default: %s)\n"
        "  --dry-run         Simulate all steps without making any changes\n"
        "  --show-config     Print active configuration and exit\n"
        "  --no-rollback     Disable automatic rollback on failure\n"
        "  --no-snapshot     Skip Btrfs/ZFS snapshot\n"
        "  -h, --help        Show this help\n"
        "  -v, --version     Print version\n"
        "\n"
        "Files:\n"
        "  Config:   %s\n"
        "  Backups:  (see backup_base in config)\n"
        "  Log:      (see log_file in config)\n",
        argv0, argv0, argv0, DEFAULT_CONF_FILE, DEFAULT_CONF_FILE);
}

static int run_update(int auto_rollback, int do_snapshot) {
    printf("==========================================\n");
    printf("  %s v%s%s\n", PROG_NAME, NS_VERSION,
           g_dry_run ? "  [DRY-RUN]" : "");
    printf("==========================================\n\n");

    if (g_dry_run) {
        printf("  Dry-run mode: no changes will be made.\n");
        cfg_print();
    }

    /* Root check is already done in main() before lock is acquired.
     * We keep a defensive check here only for the non-dry-run path
     * in case run_update() is ever called directly in future. */
    if (geteuid() != 0 && !g_dry_run) {
        fprintf(stderr, "Root privileges required.\n");
        return EXIT_ERR_PERM;
    }
    if (!g_dry_run && check_pacman_lock() != 0) return EXIT_ERR_LOCK;

    int pending = show_pending_updates();
    if (pending == 1) return EXIT_OK;
    if (pending < 0)  return EXIT_ERR_GENERIC;
    if (g_interrupted) { LOGW("Interrupted"); return EXIT_CANCELLED; }

    if (make_backup_path(g_backup_dir, sizeof(g_backup_dir)) != 0) return EXIT_ERR_BACKUP;
    printf("\n  Backup: %s\n", g_backup_dir);
    LOGI("Backup dir: %s", g_backup_dir);

    if (create_backup_dir(g_backup_dir) != 0) return EXIT_ERR_BACKUP;

    // Create a filesystem snapshot before touching anything
    char ts[32];
    make_timestamp(ts, sizeof(ts));
    FsType fstype = FS_OTHER;
    if (do_snapshot) {
        fstype = detect_root_fs();
        if      (fstype == FS_BTRFS) btrfs_snapshot_create(ts);
        else if (fstype == FS_ZFS)   zfs_snapshot_create(ts);
    }

    if (g_interrupted) { cleanup_partial_backup(g_backup_dir); return EXIT_CANCELLED; }

    printf("\n-- Creating backup --\n");
    if (do_full_backup(g_backup_dir) != 0) {
        cleanup_partial_backup(g_backup_dir);
        return EXIT_ERR_BACKUP;
    }

    if (g_interrupted) { cleanup_partial_backup(g_backup_dir); return EXIT_CANCELLED; }

    // Mark backup complete before the update so rollback can reference it immediately
    write_sentinel(g_backup_dir);

    const char *snap = (fstype == FS_BTRFS) ? g_btrfs_snap :
                       (fstype == FS_ZFS)   ? g_zfs_snap   : NULL;
    write_recovery_guide(g_backup_dir, fstype, snap);

    notify_send(NOTIFY_BACKUP_DONE, g_backup_dir);

    int upd_rc = do_update_with_checks(g_backup_dir, auto_rollback);

    // Refresh the guide with the post-update package versions
    write_recovery_guide(g_backup_dir, fstype, snap);

    cleanup_old_backups();

    printf("\n-- Summary --\n");
    printf("  Backup:   %s\n", g_backup_dir);
    printf("  Log:      %s\n", g_cfg.log_file);
    if (snap && snap[0])
        printf("  Snapshot: %s\n", snap);
    printf("  Recovery: %s/%s\n", g_backup_dir, RECOVERY_FILE);

    if (g_dry_run) {
        printf("\n  [DRY-RUN] No changes were made.\n");
        notify_send(NOTIFY_DRY_RUN_DONE, NULL);
        return EXIT_OK;
    }

    if (upd_rc == 0)  { printf("\n  Done.\n");                           return EXIT_OK; }
    if (upd_rc == 1)  { printf("\n  Update failed — rollback applied.\n"); return EXIT_ROLLBACK_OK; }
    return EXIT_ERR_UPDATE;
}

int main(int argc, char *argv[]) {
    cfg_load_defaults();

    // Pre-scan for --config before log_init so the correct log path is used
    const char *conf_path = DEFAULT_CONF_FILE;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--config") == 0) { conf_path = argv[i + 1]; break; }
    }
    cfg_parse_file(conf_path);

    log_init();
    signals_setup();
    atexit(do_cleanup);

    // Support invocation as a standalone rollback binary
    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];
    int mode_rollback = (strcmp(base, "narch-rollback") == 0);

    int  auto_rollback = g_cfg.auto_rollback;
    int  do_snapshot   = g_cfg.do_snapshot;
    int  show_config   = 0;
    char rollback_dir[PATH_MAX] = {0};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help")    == 0 || strcmp(argv[i], "-h") == 0) { print_usage(argv[0]); return EXIT_OK; }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) { printf("%s v%s\n", PROG_NAME, NS_VERSION); return EXIT_OK; }
        if (strcmp(argv[i], "--dry-run")     == 0) { g_dry_run     = 1; continue; }
        if (strcmp(argv[i], "--show-config") == 0) { show_config   = 1; continue; }
        if (strcmp(argv[i], "--no-rollback") == 0) { auto_rollback = 0; continue; }
        if (strcmp(argv[i], "--no-snapshot") == 0) { do_snapshot   = 0; continue; }
        if (strcmp(argv[i], "--config")      == 0) { i++;               continue; }
        if (strcmp(argv[i], "rollback")      == 0) { mode_rollback = 1; continue; }
        if (strcmp(argv[i], "--last")        == 0) { mode_rollback = 1; continue; }

        // Positional backup directory argument for explicit rollback
        if (mode_rollback && argv[i][0] == '/') {
            snprintf(rollback_dir, sizeof(rollback_dir), "%s", argv[i]);
            continue;
        }

        fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        print_usage(argv[0]);
        return EXIT_ERR_GENERIC;
    }

    if (show_config) { cfg_print(); return EXIT_OK; }

    LOGI("=== %s v%s start (dry_run=%d) ===", PROG_NAME, NS_VERSION, g_dry_run);

    if (geteuid() != 0 && !g_dry_run) {
        fprintf(stderr, "Root privileges required. Use sudo.\n");
        return EXIT_ERR_PERM;
    }

    if (!g_dry_run) {
        g_lock_fd = lock_acquire();
        if (g_lock_fd < 0) return EXIT_ERR_LOCK;
    }

    int rc;
    if (mode_rollback) {
        LOGI("Mode: rollback");
        if (rollback_dir[0] != '\0') {
            /* Validate directory existence without TOCTOU — open and stat */
            int dfd = open(rollback_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (dfd < 0) {
                LOGE("Backup directory not found: %s: %s",
                     rollback_dir, strerror(errno));
                return EXIT_ERR_GENERIC;
            }
            close(dfd);
            rc = do_rollback(rollback_dir);
        } else {
            rc = rollback_from_last();
        }
        if (rc == 0) notify_send(NOTIFY_ROLLBACK_OK, NULL);
        else         notify_send(NOTIFY_ROLLBACK_FAIL, NULL);
        return rc == 0 ? EXIT_OK : EXIT_ERR_GENERIC;
    }

    LOGI("Mode: update (auto_rollback=%d snapshot=%d dry_run=%d)",
         auto_rollback, do_snapshot, g_dry_run);
    rc = run_update(auto_rollback, do_snapshot);
    return rc;
}
