#pragma once
#include "common.h"

int  lock_acquire(void);
void lock_release(int fd);
