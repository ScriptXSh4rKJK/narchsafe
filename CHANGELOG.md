# Changelog

All notable changes to narchsafe will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.2.0] - 2025-03-08

### Fixed — Security
- **`proc.c`** — `pipe()` replaced with `pipe2(O_CLOEXEC)` in `run_tee` and `run_capture` to prevent pipe file-descriptor leaks into child processes across `fork`+`exec`
- **`proc.c`** — `run_to_file`: `child_init()` was called *before* `dup2()`, which closed the log fd before stdout was redirected; order corrected; stderr of child now redirected to `/dev/null` instead of leaking to the terminal
- **`proc.c`** — `run_capture`: same `child_init` order bug fixed; added buffer-overflow guard on the `read` loop
- **`notify.c`** — `_notify_libnotify`: replaced `execlp` (PATH-search) with `execve` using the hardcoded `NOTIFY_SEND_BIN` path and `SAFE_ENV`; prevents PATH-hijacking
- **`notify.c`** — `_notify_telegram`: replaced `execlp` with `execve`+`SAFE_ENV`; fixed `/dev/null` fd leak (was closed before `dup2` consumed it)
- **`checks.c`** — `check_pacman_lock`: replaced `access(F_OK)` (TOCTOU) with `open()+close()`
- **`main.c`** — rollback directory validation: replaced `access(F_OK)` (TOCTOU) with `open(O_DIRECTORY)+close()`

### Fixed — Correctness
- **`lock.c`** — added `fsync()` after writing PID to lock file so the PID is visible to other processes immediately
- **`lock.c`** — `read()` return value now checked with proper sign before being used to index `pidbuf`
- **`rollback.c`** — `fdopen()` failure now correctly closes the underlying fd instead of leaking it
- **`rollback.c`** — rollback log now `fflush()`+`fsync()`+`fclose()` on exit to ensure all data is written
- **`rollback.c`** — replaced `strncpy` with `memcpy`+explicit NUL in both `bsearch` key construction loops to guarantee NUL-termination
- **`update.c`** — `open_update_log`: `fdopen()` failure now correctly closes the fd
- **`backup.c`** — `chmod()` return value checked; `chmod` now correctly skipped in dry-run mode
- **`snapshot.c`** — `/proc/mounts` parse buffers enlarged (device paths can exceed 256 chars on LVM/LUKS systems)

### Changed
- Version bumped to `1.2.0`
- `etc/narchsafe.conf` fully rewritten with step-by-step Telegram setup instructions and clearer comments for every option

## [1.1.0] - 2025-02-01

### Fixed
- Telegram notification: chat_id and message now passed as separate `--data-urlencode` args (injection fix)
- Lock file path changed from `/var/run/narchsafe.lock` to `/run/narchsafe.lock`
- Coreutils paths updated to `/usr/bin/*` (modern Arch unified `/usr`)
- `checks.c`: added `snprintf` truncation checks, restored `access()` check for kernel module directory
- `utils.c`: `cleanup_old_backups` and `find_last_backup` now handle `DT_UNKNOWN` via `stat()` fallback
- Added compiler hardening flags: `-fstack-protector-strong -fPIE -pie -Wl,-z,relro,-z,now,-z,noexecstack`

## [1.0.0] - 2025-01-01

### Added
- Initial release: backup, update, rollback, Btrfs/ZFS snapshots, Telegram + libnotify notifications

[Unreleased]: https://github.com/ScriptXSh4rKJK/narchsafe/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/ScriptXSh4rKJK/narchsafe/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/ScriptXSh4rKJK/narchsafe/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/ScriptXSh4rKJK/narchsafe/releases/tag/v1.0.0
