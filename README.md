# NarchSafe

A safe `pacman -Syu` wrapper for Arch Linux. Creates a full backup before every update and automatically rolls back if something goes wrong.

## Features

- Backs up `/boot`, `/etc`, and the pacman database before updating
- Auto-rollback if the update fails, system enters a critical state, or kernel install looks broken
- Btrfs / ZFS snapshot support
- Desktop notifications via `notify-send`
- Telegram bot notifications via `curl`
- `--dry-run` — simulate everything without touching the system (no root needed)
- Config file at `/etc/narchsafe.conf`
- `narch-rollback --last` for quick manual rollback

## Install

```bash
git clone https://github.com/YOUR_USERNAME/narchsafe.git
cd narchsafe
make
sudo make install
```

This installs:
- `/usr/local/bin/narchsafe`
- `/usr/local/bin/narch-rollback` (symlink)
- `/etc/narchsafe.conf`

## Usage

```bash
sudo narchsafe                    # update with backup + auto-rollback
sudo narchsafe --dry-run          # simulate, no changes made
sudo narchsafe --no-rollback      # skip auto-rollback
sudo narchsafe --no-snapshot      # skip Btrfs/ZFS snapshot
sudo narchsafe --show-config      # print active configuration

narch-rollback --last             # roll back to the latest backup
narchsafe rollback --last         # same
narchsafe rollback /path/to/backup
```

## Configuration

Edit `/etc/narchsafe.conf`:

```ini
backup_base       = /var/lib/narchsafe/backups
keep_count        = 5
boot_compress     = no    # yes = tar.gz, no = cp -a
auto_rollback     = yes
snapshot          = yes

# Telegram notifications (optional)
notify_telegram   = yes
telegram_token    = YOUR_BOT_TOKEN
telegram_chat_id  = YOUR_CHAT_ID
```

All keys are optional — missing keys fall back to defaults.

## How rollback works

1. Restores `/boot` from the backup
2. Reads `versions_before.txt` (saved pre-update)
3. Downgrades changed packages from the pacman cache
4. Removes packages that weren't present before the update
5. Writes `rollback.log` with full details

A `recovery.txt` with copy-paste commands for every scenario is written into each backup directory.

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Generic error |
| 2 | Not root |
| 3 | Lock error |
| 4 | Backup error |
| 5 | Update failed (no rollback or rollback failed) |
| 6 | Cancelled by signal |
| 7 | Update failed but rollback succeeded |

## Requirements

- `pacman`, `tar`, `cp`, `rm` — standard on Arch
- `btrfs-progs` — only for Btrfs snapshots
- `libnotify` — only for desktop notifications
- `curl` — only for Telegram notifications

## License

MIT
