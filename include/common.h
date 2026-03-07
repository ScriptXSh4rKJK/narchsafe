#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/file.h>
#include <syslog.h>

#include "config.h"

typedef enum {
    EXIT_OK          = 0,
    EXIT_ERR_GENERIC = 1,
    EXIT_ERR_PERM    = 2,
    EXIT_ERR_LOCK    = 3,
    EXIT_ERR_BACKUP  = 4,
    EXIT_ERR_UPDATE  = 5,
    EXIT_CANCELLED   = 6,
    EXIT_ROLLBACK_OK = 7,
} ExitCode;

typedef enum {
    FS_OTHER = 0,
    FS_BTRFS,
    FS_ZFS,
} FsType;

extern volatile sig_atomic_t  g_interrupted;
extern char                   g_backup_dir[PATH_MAX];
extern int                    g_lock_fd;
extern char                   g_btrfs_snap[PATH_MAX];
extern char                   g_zfs_snap[512];
extern FILE                  *g_logfile;

// Set by --dry-run; checked by run_mut() / run_tee_mut()
extern int g_dry_run;
