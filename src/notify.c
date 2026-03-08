#include "notify.h"
#include "cfg.h"
#include "log.h"
#include "proc.h"

static void _notify_libnotify(const char *title, const char *body, const char *icon) {
    if (access(NOTIFY_SEND_BIN, X_OK) != 0) {
        LOGW("notify-send not found at %s", NOTIFY_SEND_BIN);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) { LOGW("fork for notify-send: %s", strerror(errno)); return; }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        /* Use execve with hardcoded path — never rely on PATH search */
        if (icon && icon[0]) {
            char *av[] = { NOTIFY_SEND_BIN,
                           "--app-name=" PROG_NAME,
                           "-i", (char *)icon,
                           "--", (char *)title, (char *)body, NULL };
            execve(NOTIFY_SEND_BIN, av, SAFE_ENV);
        } else {
            char *av[] = { NOTIFY_SEND_BIN,
                           "--app-name=" PROG_NAME,
                           "--", (char *)title, (char *)body, NULL };
            execve(NOTIFY_SEND_BIN, av, SAFE_ENV);
        }
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        LOGW("notify-send exited with %d", WEXITSTATUS(status));
}

static void _escape_html(const char *src, char *dst, size_t dstsz) {
    size_t i = 0;
    while (*src && i + 10 < dstsz) {
        if      (*src == '<') { memcpy(dst+i, "&lt;",  4); i += 4; }
        else if (*src == '>') { memcpy(dst+i, "&gt;",  4); i += 4; }
        else if (*src == '&') { memcpy(dst+i, "&amp;", 5); i += 5; }
        else                  { dst[i++] = *src; }
        src++;
    }
    dst[i] = '\0';
}

static void _notify_telegram(const char *title, const char *body) {
    if (!g_cfg.telegram_token[0] || !g_cfg.telegram_chat_id[0]) {
        LOGW("Telegram: token or chat_id not configured");
        return;
    }
    if (access(CURL_BIN, X_OK) != 0) {
        LOGW("curl not found at %s, skipping Telegram notification", CURL_BIN);
        return;
    }

    char etitle[256], ebody[1024];
    _escape_html(title, etitle, sizeof(etitle));
    _escape_html(body,  ebody,  sizeof(ebody));

    char msg[1536];
    snprintf(msg, sizeof(msg), "<b>%s</b>\n%s", etitle, ebody);

    char url[768];
    snprintf(url, sizeof(url), "%s/bot%s/sendMessage",
             g_cfg.telegram_api_url, g_cfg.telegram_token);

    LOGI("Sending Telegram notification");

    /* Pass each field separately so curl handles URL-encoding correctly.
     * Never concatenate chat_id or msg into a single --data-urlencode arg —
     * that is an injection vector if either field contains special characters. */
    pid_t pid = fork();
    if (pid < 0) { LOGW("fork for curl: %s", strerror(errno)); return; }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            /* devnull fd closed by O_CLOEXEC on exec */
        }

        /*
         * For --data-urlencode the correct form is:
         *   --data-urlencode "name=value"   → curl encodes only 'value'
         * We build each param string separately so no field value can
         * escape into the key name (injection prevention).
         */
        char chat_id_arg[128];
        snprintf(chat_id_arg, sizeof(chat_id_arg),
                 "chat_id=%s", g_cfg.telegram_chat_id);

        /* msg already has HTML escaping applied */
        char text_arg[1600];
        snprintf(text_arg, sizeof(text_arg), "text=%s", msg);

        char *av[] = {
            CURL_BIN, "-sS", "--max-time", "10",
            "-X", "POST", (char *)url,
            "--data-urlencode", chat_id_arg,
            "--data-urlencode", "parse_mode=HTML",
            "--data-urlencode", text_arg,
            NULL
        };
        execve(CURL_BIN, av, SAFE_ENV);
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        LOGW("curl (Telegram) exited with %d", WEXITSTATUS(status));
    else
        LOGI("Telegram notification sent");
}

void notify_message(const char *title, const char *body, const char *icon) {
    if (g_cfg.notify_libnotify) _notify_libnotify(title, body, icon);
    if (g_cfg.notify_telegram)  _notify_telegram(title, body);
}

void notify_send(NotifyEvent event, const char *detail) {
    const char *title = PROG_NAME;
    char body[512]    = {0};
    const char *icon  = "dialog-information";

    switch (event) {
    case NOTIFY_UPDATE_START:
        snprintf(body, sizeof(body), "Starting system update...");
        icon = "system-software-update";
        break;
    case NOTIFY_BACKUP_DONE:
        snprintf(body, sizeof(body), "Backup created:\n%s", detail ? detail : "");
        icon = "drive-harddisk";
        break;
    case NOTIFY_UPDATE_OK:
        snprintf(body, sizeof(body), "System updated successfully.");
        icon = "emblem-default";
        break;
    case NOTIFY_UPDATE_FAIL:
        snprintf(body, sizeof(body), "Update FAILED.%s%s",
                 detail ? "\n" : "", detail ? detail : "");
        icon = "dialog-error";
        break;
    case NOTIFY_ROLLBACK_OK:
        snprintf(body, sizeof(body), "Rollback completed successfully.\n%s",
                 detail ? detail : "");
        icon = "edit-undo";
        break;
    case NOTIFY_ROLLBACK_FAIL:
        snprintf(body, sizeof(body), "Rollback FAILED.\n%s",
                 detail ? detail : "");
        icon = "dialog-error";
        break;
    case NOTIFY_DRY_RUN_DONE:
        snprintf(body, sizeof(body), "[DRY-RUN] Simulation complete — no changes were made.");
        icon = "dialog-information";
        break;
    }

    LOGI("Notification: %s", body);
    notify_message(title, body, icon);
}
