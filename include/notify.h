#pragma once
#include "common.h"

typedef enum {
    NOTIFY_UPDATE_START = 0,
    NOTIFY_BACKUP_DONE,
    NOTIFY_UPDATE_OK,
    NOTIFY_UPDATE_FAIL,
    NOTIFY_ROLLBACK_OK,
    NOTIFY_ROLLBACK_FAIL,
    NOTIFY_DRY_RUN_DONE,
} NotifyEvent;

// Dispatch a structured event through all configured channels
void notify_send(NotifyEvent event, const char *detail);

// Send a raw message; icon is an XDG icon name used by notify-send
void notify_message(const char *title, const char *body, const char *icon);
