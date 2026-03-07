#pragma once
#include "common.h"

FILE *open_update_log(const char *backup_dir);
int   do_update_logged(FILE *logfp);
int   backup_versions_after(const char *backup_dir);

// Returns 0 = success, 1 = failed but rollback succeeded, -1 = failure
int do_update_with_checks(const char *backup_dir, int auto_rollback);
