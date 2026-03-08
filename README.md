# narchsafe

**Safe system updater for Arch Linux.**  
Automatic backup → optional Btrfs/ZFS snapshot → `pacman -Syu` →
post-update checks → auto-rollback if anything goes wrong.

[![CI](https://github.com/ScriptXSh4rKJK/narchsafe/actions/workflows/ci.yml/badge.svg)](https://github.com/ScriptXSh4rKJK/narchsafe/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-1.2.0-green.svg)](CHANGELOG.md)

---

## The problem

Arch Linux is a rolling release. Every `pacman -Syu` can break your system —
a bad kernel update, a broken config in `/etc`, a package that removes a
dependency something else needed. If you run it without a safety net, the
only way back is a live USB and a lot of manual work.

`narchsafe` is that safety net.

---

## What it does

```
narchsafe
    │
    ├── 1. Check for pending updates  (pacman -Qu)
    ├── 2. Create timestamped backup
    │       ├── Package list           (packages.txt)
    │       ├── Package versions       (versions_before.txt)
    │       ├── /boot                  (cp -a  or  tar.gz)
    │       ├── /etc                   (tar.gz)
    │       └── Pacman local DB        (pacman_local_db/)
    ├── 3. Btrfs / ZFS snapshot        (if enabled)
    ├── 4. pacman -Syu --noconfirm     (output tee'd to update.log)
    ├── 5. Post-update checks
    │       ├── systemctl is-system-running
    │       └── kernel image freshness
    ├── 6. Auto-rollback on failure
    │       ├── Restore /boot from backup
    │       ├── pacman -U   (downgrade changed packages from cache)
    │       └── pacman -Rns (remove packages added during update)
    └── 7. Write recovery.txt with full manual recovery steps
```

---

## Quick start

### 1. Install

```bash
git clone https://github.com/ScriptXSh4rKJK/narchsafe.git
cd narchsafe
make
sudo make install
```

This installs:
- `/usr/local/bin/narchsafe`
- `/usr/local/bin/narch-rollback`  ← symlink for rollback-only use
- `/etc/narchsafe.conf`            ← created only if it does not exist yet
- `/var/lib/narchsafe/backups/`    ← mode 0700

### 2. Configure (optional — defaults work on standard Arch)

```bash
sudo nano /etc/narchsafe.conf
```

The most common tweaks:

```ini
# How many backups to keep (older ones pruned automatically)
keep_count = 5

# Minimum free space on backup filesystem before aborting (MB)
min_free_mb = 512

# Compress /boot as tar.gz instead of cp -a
boot_compress = yes

# Roll back automatically if any check fails
auto_rollback = yes
```

To enable **Telegram notifications** (full setup steps are in the config file):

```ini
notify_telegram  = yes
telegram_token   = 123456789:AAxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
telegram_chat_id = -100xxxxxxxxxx
```

### 3. Run

```bash
sudo narchsafe
```

### 4. Preview without making any changes

```bash
sudo narchsafe --dry-run
```

---

## Usage

```
Usage:
  narchsafe [OPTIONS]               Update system with backup
  narchsafe rollback --last         Roll back to the latest backup
  narchsafe rollback <DIR>          Roll back from a specific backup directory
  narch-rollback --last             Alias for 'rollback --last'

Options:
  --config <FILE>   Config file path (default: /etc/narchsafe.conf)
  --dry-run         Simulate all steps without making any changes
  --show-config     Print active configuration and exit
  --no-rollback     Disable automatic rollback on failure
  --no-snapshot     Skip Btrfs/ZFS snapshot
  -h, --help        Show this help
  -v, --version     Print version
```

---

## Example output

```
==========================================
  narchsafe v1.2.0
==========================================

Pending updates:
  core/linux 6.12.1.arch1-1 -> 6.12.2.arch1-1
  core/linux-headers 6.12.1.arch1-1 -> 6.12.2.arch1-1
  extra/firefox 132.0-1 -> 133.0-1

  Backup: /var/lib/narchsafe/backups/2025-03-08_14-22-07

-- Creating backup --
[1/5] Saving package list...
[2/5] Saving package versions...
[3/5] Backing up /boot (cp -a)...
[4/5] Archiving /etc...
[5/5] Saving pacman database...

-- Package cache check --
All 1423 packages present in cache.

  Btrfs snapshot: /@narchsafe-2025-03-08_14-22-07

==========================================
  RUNNING SYSTEM UPDATE
==========================================

:: Synchronising package databases...
:: Starting full system upgrade...
...

  Update complete.

-- System health (systemctl) --
  Status: running
  All units running normally.

-- Kernel install check --
  /boot/vmlinuz-linux updated 12 second(s) ago.
  Kernel modules found: /usr/lib/modules/6.12.2-arch1-1

-- Summary --
  Backup:   /var/lib/narchsafe/backups/2025-03-08_14-22-07
  Log:      /var/log/narchsafe.log
  Snapshot: /@narchsafe-2025-03-08_14-22-07
  Recovery: /var/lib/narchsafe/backups/2025-03-08_14-22-07/recovery.txt

  Done.
```

---

## Rollback

### Automatic

With `auto_rollback = yes` (default) narchsafe rolls back automatically when
any check fails. You will see:

```
  Triggering auto-rollback: kernel install check failed
  ...
  System rolled back to pre-update state.
```

### Manual — from a working shell (single-user mode or SSH)

```bash
sudo narch-rollback --last
# or
sudo narchsafe rollback /var/lib/narchsafe/backups/2025-03-08_14-22-07
```

### Manual — from a live USB

Every backup directory contains `recovery.txt` with full step-by-step
instructions for restoring without narchsafe. It covers:

- Restoring `/boot` (both tar.gz and directory formats)
- Restoring `/etc`
- Restoring the pacman database
- Manual `pacman -U` downgrade from cache
- Btrfs and ZFS snapshot rollback

---

## Backup layout

```
/var/lib/narchsafe/backups/
└── 2025-03-08_14-22-07/
    ├── .complete              ← sentinel; absent = backup is incomplete
    ├── packages.txt           ← explicitly installed packages (pacman -Qqe)
    ├── versions_before.txt    ← all installed versions before the update
    ├── versions_after.txt     ← all installed versions after the update
    ├── boot/                  ← /boot copy  (boot_compress = no)
    ├── boot_backup.tar.gz     ← /boot archive  (boot_compress = yes)
    ├── etc_backup.tar.gz      ← /etc archive
    ├── pacman_local_db/       ← copy of /var/lib/pacman/local
    ├── update.log             ← raw pacman -Syu output
    ├── rollback.log           ← written if rollback was triggered
    └── recovery.txt           ← manual recovery guide
```

---

## Running automatically

### systemd timer (recommended)

`/etc/systemd/system/narchsafe.service`:

```ini
[Unit]
Description=narchsafe — safe Arch system update
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/narchsafe
```

`/etc/systemd/system/narchsafe.timer`:

```ini
[Unit]
Description=Run narchsafe weekly

[Timer]
OnCalendar=weekly
Persistent=true
RandomizedDelaySec=1h

[Install]
WantedBy=timers.target
```

```bash
sudo systemctl enable --now narchsafe.timer
```

### cron

```cron
0 3 * * 0  root  /usr/local/bin/narchsafe >> /var/log/narchsafe-cron.log 2>&1
```

---

## Dependencies

| Dependency | Required | Notes |
|---|---|---|
| `gcc` or `clang` | Build only | C11 compiler |
| `make` | Build only | |
| `pacman` | Runtime | Core functionality |
| `systemd` | Runtime | `systemctl` health check |
| `tar` | Runtime | `/etc` backup and `/boot` compress option |
| `btrfs-progs` | Optional | `pacman -S btrfs-progs` for Btrfs snapshots |
| `zfs-utils` | Optional | ZFS snapshots |
| `libnotify` | Optional | `pacman -S libnotify` for desktop notifications |
| `curl` | Optional | For Telegram notifications (usually pre-installed) |

## Uninstall

```bash
sudo make uninstall
# Remove data and config:
sudo rm -rf /var/lib/narchsafe /var/log/narchsafe.log /etc/narchsafe.conf
```

---

## Security

- All child processes run via `execve()` with a minimal hardcoded environment — no shell, no PATH lookup
- Telegram token never passed on the command line; always via `--data-urlencode`
- All file descriptors opened with `O_CLOEXEC`; pipes created with `pipe2(O_CLOEXEC)`
- Backup dirs and log files created with mode `0600`/`0700`
- Binary built with `-fstack-protector-strong -fPIE -pie -Wl,-z,relro,-z,now,-z,noexecstack`
- File existence checks use `open()+fstat()` instead of `access()` to prevent TOCTOU races

---

## Related projects

- **[narchstab](https://github.com/ScriptXSh4rKJK/narchstab)** — OOM configuration, dmesg hardware scanner, failed unit reporter

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE)
