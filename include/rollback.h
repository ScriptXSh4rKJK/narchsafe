#pragma once
#include "common.h"

typedef struct {
    char name[128];
    char ver[128];
} PkgInfo;

int  parse_pkg_file(FILE *fp, PkgInfo *arr, int max);
void sort_pkgs(PkgInfo *arr, int n);
int  find_pkg_in_cache(const char *name, const char *ver, char *out, size_t outsz);

// Returns 0 on success, -1 on failure
int do_rollback(const char *backup_dir);
int rollback_from_last(void);

void write_recovery_guide(const char *backup_dir, FsType fstype, const char *snap_name);
