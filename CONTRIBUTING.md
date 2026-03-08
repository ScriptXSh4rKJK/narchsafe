# Contributing to narchsafe

Thank you for considering a contribution. This document explains how to
get started, what standards the code follows, and how to submit changes.

---

## Table of Contents
- [Getting Started](#getting-started)
- [Code Style](#code-style)
- [Building and Testing](#building-and-testing)
- [Submitting a Pull Request](#submitting-a-pull-request)
- [Reporting Bugs](#reporting-bugs)
- [Security Issues](#security-issues)

---

## Getting Started

```bash
git clone https://github.com/ScriptXSh4rKJK/narchsafe.git
cd narchsafe
make
```

Create a feature branch before making changes:
```bash
git checkout -b feat/your-feature-name
```

---

## Code Style

The project is written in **C11**. Please follow these conventions:

- **Indentation:** 4 spaces, no tabs
- **Line length:** 90 characters soft limit, 120 hard limit
- **Naming:** `snake_case` for functions/variables, `UPPER_CASE` for macros, `PascalCase` for types
- **Error handling:** every syscall that can fail must check its return value
- **No `system()`:** use `execve()` via the `run*` helpers in `proc.c`
- **No `sprintf`/`strcpy`/`strncpy`:** use `snprintf`/`memcpy` with explicit sizes
- **No `access()`:** use `open()+fstat()` to avoid TOCTOU races
- **Pipes:** always use `pipe2(O_CLOEXEC)` instead of `pipe()`
- **File descriptors:** mark with `O_CLOEXEC` at creation time

### Compiler check

All code must compile with **zero warnings** under:
```
-std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Wformat-security
-Wshadow -Wstrict-prototypes -Wdouble-promotion -Wnull-dereference
-O2 -fstack-protector-strong -fPIE
```

```bash
make clean && make CC=gcc
make clean && make CC=clang   # if available
```

---

## Building and Testing

```bash
make              # build
make clean        # remove artifacts
sudo make install # install to /usr/local/bin
```

### Dry-run smoke test (no root needed)
```bash
./narchsafe --dry-run
./narchsafe --show-config
./narchsafe --version
./narchsafe --dry-run rollback --last
```

---

## Submitting a Pull Request

1. One logical change per PR
2. Update `CHANGELOG.md` under `[Unreleased]`
3. Update `README.md` if user-facing behaviour changes
4. Ensure CI passes (GitHub Actions runs on every push)
5. Clear PR description: what problem, how tested, known limitations

---

## Reporting Bugs

Open a [GitHub Issue](https://github.com/ScriptXSh4rKJK/narchsafe/issues) and include:

- `narchsafe --version`
- Arch Linux kernel version (`uname -r`)
- systemd version (`systemctl --version | head -1`)
- Full command and terminal output
- Relevant lines from `/var/log/narchsafe.log`

---

## Security Issues

**Do not open a public issue for security vulnerabilities.**

Use [GitHub private vulnerability reporting](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing/privately-reporting-a-security-vulnerability).

Include: description, steps to reproduce, potential impact, suggested fix.
We aim to respond within 72 hours.
