#pragma once
#include "common.h"

int  create_backup_dir(const char *dir);
int  backup_package_list(const char *dir);
int  backup_versions(const char *dir);
int  backup_boot(const char *dir);
int  backup_etc(const char *dir);
int  backup_pacman_db(const char *dir);
void check_cache_integrity(const char *backup_dir);
int  do_full_backup(const char *backup_dir);
