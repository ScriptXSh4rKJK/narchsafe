#pragma once
#include "common.h"

unsigned long long free_bytes(const char *path);
int  make_backup_path(char *buf, size_t size);
void make_timestamp(char *buf, size_t size);
int  write_sentinel(const char *dir);
void cleanup_partial_backup(const char *dir);
void cleanup_old_backups(void);
int  find_last_backup(char *buf, size_t bufsz);
