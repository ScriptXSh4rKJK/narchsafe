#pragma once
#include "common.h"

typedef struct {
    char backup_base[PATH_MAX];
    char pacman_cache[PATH_MAX];
    char log_file[PATH_MAX];
    char lock_file[PATH_MAX];

    int                keep_count;
    int                boot_compress;   // 0 = cp -a, 1 = tar.gz
    unsigned long long min_free_bytes;

    int  kernel_fresh_secs;
    int  auto_rollback;
    int  do_snapshot;

    int  notify_libnotify;
    int  notify_telegram;
    char telegram_token[256];
    char telegram_chat_id[64];
    char telegram_api_url[256];
} NarchsafeConfig;

extern NarchsafeConfig g_cfg;

void cfg_load_defaults(void);
int  cfg_parse_file(const char *path);
void cfg_print(void);
