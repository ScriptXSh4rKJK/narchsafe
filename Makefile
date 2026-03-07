CC      ?= gcc
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -O2 \
           -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
           -Iinclude
LDFLAGS  =

TARGET   = narchsafe

SRCS = src/main.c      \
       src/cfg.c       \
       src/log.c       \
       src/lock.c      \
       src/proc.c      \
       src/utils.c     \
       src/snapshot.c  \
       src/backup.c    \
       src/checks.c    \
       src/notify.c    \
       src/update.c    \
       src/rollback.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

DESTDIR   ?=
PREFIX    ?= /usr/local
BINDIR     = $(DESTDIR)$(PREFIX)/bin
CONFDIR    = $(DESTDIR)/etc
BACKUPDIR  = $(DESTDIR)/var/lib/narchsafe/backups

install: $(TARGET)
	install -Dm755 $(TARGET)            $(BINDIR)/$(TARGET)
	ln -sf $(PREFIX)/bin/$(TARGET)      $(BINDIR)/narch-rollback
	install -Dm644 etc/narchsafe.conf   $(CONFDIR)/narchsafe.conf
	install -d -m700                    $(BACKUPDIR)

uninstall:
	rm -f $(BINDIR)/$(TARGET) $(BINDIR)/narch-rollback

clean:
	rm -f $(OBJS) $(TARGET) $(OBJS:.o=.d)

-include $(OBJS:.o=.d)
