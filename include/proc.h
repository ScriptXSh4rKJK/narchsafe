#pragma once
#include "common.h"

extern char *const SAFE_ENV[];

void child_init(void);
int  wait_child(pid_t pid, const char *name);

// Always execute — safe for read-only operations
int run(const char *path, char *const argv[]);
int run_to_file(const char *path, char *const argv[], const char *outpath);
int run_tee(const char *path, char *const argv[], FILE *logfp);
int run_capture(const char *path, char *const argv[], char *buf, size_t bufsz);

// Honour g_dry_run — skip and print intent instead of executing
int run_mut(const char *path, char *const argv[]);
int run_tee_mut(const char *path, char *const argv[], FILE *logfp);
