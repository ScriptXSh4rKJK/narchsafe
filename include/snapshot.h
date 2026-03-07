#pragma once
#include "common.h"

FsType detect_root_fs(void);
int    get_zfs_root_dataset(char *buf, size_t size);
int    btrfs_snapshot_create(const char *ts);
int    zfs_snapshot_create(const char *ts);
