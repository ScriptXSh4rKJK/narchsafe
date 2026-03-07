#pragma once
#include "common.h"

// Returns 0 if clear, -1 if stale lock found
int check_pacman_lock(void);

// Returns 0 = updates available, 1 = nothing to update, -1 = error
int show_pending_updates(void);

// Returns 0 = running, 1 = degraded (warn), -1 = critical
int check_systemctl_health(void);

// Returns 0 if kernel looks healthy, -1 on suspected install failure
int check_kernel_update(const char *backup_dir);
