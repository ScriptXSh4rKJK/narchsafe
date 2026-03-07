#include "cfg.h"
#include "log.h"

NarchsafeConfig g_cfg;

static void trim_inplace(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n'))
        s[--len] = '\0';
}

static void strip_inline_comment(char *s) {
    int in_val = 0;
    for (char *p = s; *p; p++) {
        if (!in_val && (*p == ' ' || *p == '\t')) continue;
        in_val = 1;
        if (*p == '#' || *p == ';') { *p = '\0'; break; }
    }
    trim_inplace(s);
}

static int parse_bool(const char *s) {
    if (!s) return -1;
    if (strcmp(s,"1")==0 || strcasecmp(s,"yes")==0  ||
        strcasecmp(s,"true")==0 || strcasecmp(s,"on")==0)  return 1;
    if (strcmp(s,"0")==0 || strcasecmp(s,"no")==0   ||
        strcasecmp(s,"false")==0 || strcasecmp(s,"off")==0) return 0;
    return -1;
}

void cfg_load_defaults(void) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    snprintf(g_cfg.backup_base,      sizeof(g_cfg.backup_base),      "%s", DEFAULT_BACKUP_BASE);
    snprintf(g_cfg.pacman_cache,     sizeof(g_cfg.pacman_cache),     "%s", DEFAULT_PACMAN_CACHE);
    snprintf(g_cfg.log_file,         sizeof(g_cfg.log_file),         "%s", DEFAULT_LOG_FILE);
    snprintf(g_cfg.lock_file,        sizeof(g_cfg.lock_file),        "%s", DEFAULT_LOCK_FILE);
    snprintf(g_cfg.telegram_api_url, sizeof(g_cfg.telegram_api_url), "%s", DEFAULT_TELEGRAM_API_URL);

    g_cfg.keep_count        = DEFAULT_BACKUP_KEEP_COUNT;
    g_cfg.boot_compress     = DEFAULT_BOOT_COMPRESS;
    g_cfg.min_free_bytes    = (unsigned long long)DEFAULT_MIN_FREE_MB * 1024ULL * 1024ULL;
    g_cfg.kernel_fresh_secs = DEFAULT_KERNEL_FRESH_SECS;
    g_cfg.auto_rollback     = DEFAULT_AUTO_ROLLBACK;
    g_cfg.do_snapshot       = DEFAULT_DO_SNAPSHOT;
    g_cfg.notify_libnotify  = DEFAULT_NOTIFY_LIBNOTIFY;
    g_cfg.notify_telegram   = DEFAULT_NOTIFY_TELEGRAM;
}

int cfg_parse_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        // Config is optional; missing file is not an error
        LOGI("No config file at %s, using defaults", path);
        return -1;
    }
    LOGI("Loading config: %s", path);

    char line[512];
    int  lineno = 0;

    while (fgets(line, (int)sizeof(line), f)) {
        lineno++;
        trim_inplace(line);

        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') continue;

        char *eq = strchr(line, '=');
        if (!eq) {
            LOGW("%s:%d: missing '=', skipping line", path, lineno);
            continue;
        }
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim_inplace(key);
        trim_inplace(val);
        strip_inline_comment(val);

        // String keys
        if      (strcmp(key, "backup_base")      == 0) snprintf(g_cfg.backup_base,      sizeof(g_cfg.backup_base),      "%s", val);
        else if (strcmp(key, "pacman_cache")     == 0) snprintf(g_cfg.pacman_cache,     sizeof(g_cfg.pacman_cache),     "%s", val);
        else if (strcmp(key, "log_file")         == 0) snprintf(g_cfg.log_file,         sizeof(g_cfg.log_file),         "%s", val);
        else if (strcmp(key, "lock_file")        == 0) snprintf(g_cfg.lock_file,        sizeof(g_cfg.lock_file),        "%s", val);
        else if (strcmp(key, "telegram_token")   == 0) snprintf(g_cfg.telegram_token,   sizeof(g_cfg.telegram_token),   "%s", val);
        else if (strcmp(key, "telegram_chat_id") == 0) snprintf(g_cfg.telegram_chat_id, sizeof(g_cfg.telegram_chat_id), "%s", val);
        else if (strcmp(key, "telegram_api_url") == 0) snprintf(g_cfg.telegram_api_url, sizeof(g_cfg.telegram_api_url), "%s", val);

        // Integer keys
        else if (strcmp(key, "keep_count") == 0) {
            int v = atoi(val);
            if (v > 0) g_cfg.keep_count = v;
            else LOGW("%s:%d: keep_count must be > 0", path, lineno);
        } else if (strcmp(key, "min_free_mb") == 0) {
            long long v = atoll(val);
            if (v > 0) g_cfg.min_free_bytes = (unsigned long long)v * 1024ULL * 1024ULL;
            else LOGW("%s:%d: min_free_mb must be > 0", path, lineno);
        } else if (strcmp(key, "kernel_fresh_secs") == 0) {
            int v = atoi(val);
            if (v > 0) g_cfg.kernel_fresh_secs = v;
            else LOGW("%s:%d: kernel_fresh_secs must be > 0", path, lineno);

        // Boolean keys
        } else if (strcmp(key, "boot_compress")    == 0) {
            int b = parse_bool(val);
            if (b >= 0) g_cfg.boot_compress    = b;
            else LOGW("%s:%d: invalid value '%s' for boot_compress", path, lineno, val);
        } else if (strcmp(key, "auto_rollback")    == 0) {
            int b = parse_bool(val);
            if (b >= 0) g_cfg.auto_rollback    = b;
            else LOGW("%s:%d: invalid value '%s' for auto_rollback", path, lineno, val);
        } else if (strcmp(key, "snapshot")         == 0) {
            int b = parse_bool(val);
            if (b >= 0) g_cfg.do_snapshot      = b;
            else LOGW("%s:%d: invalid value '%s' for snapshot", path, lineno, val);
        } else if (strcmp(key, "notify_libnotify") == 0) {
            int b = parse_bool(val);
            if (b >= 0) g_cfg.notify_libnotify = b;
            else LOGW("%s:%d: invalid value '%s' for notify_libnotify", path, lineno, val);
        } else if (strcmp(key, "notify_telegram")  == 0) {
            int b = parse_bool(val);
            if (b >= 0) g_cfg.notify_telegram  = b;
            else LOGW("%s:%d: invalid value '%s' for notify_telegram", path, lineno, val);
        } else {
            LOGW("%s:%d: unknown key '%s'", path, lineno, key);
        }
    }

    fclose(f);
    return 0;
}

void cfg_print(void) {
    printf("\n-- Configuration (%s v%s) --\n", PROG_NAME, NS_VERSION);
    printf("  backup_base        = %s\n", g_cfg.backup_base);
    printf("  pacman_cache       = %s\n", g_cfg.pacman_cache);
    printf("  log_file           = %s\n", g_cfg.log_file);
    printf("  lock_file          = %s\n", g_cfg.lock_file);
    printf("  keep_count         = %d\n", g_cfg.keep_count);
    printf("  boot_compress      = %s\n", g_cfg.boot_compress ? "yes (tar.gz)" : "no (cp -a)");
    printf("  min_free_mb        = %llu\n", g_cfg.min_free_bytes / (1024ULL * 1024ULL));
    printf("  kernel_fresh_secs  = %d\n", g_cfg.kernel_fresh_secs);
    printf("  auto_rollback      = %s\n", g_cfg.auto_rollback ? "yes" : "no");
    printf("  snapshot           = %s\n", g_cfg.do_snapshot   ? "yes" : "no");
    printf("  notify_libnotify   = %s\n", g_cfg.notify_libnotify ? "yes" : "no");
    printf("  notify_telegram    = %s\n", g_cfg.notify_telegram  ? "yes" : "no");
    if (g_cfg.notify_telegram) {
        printf("  telegram_api_url   = %s\n", g_cfg.telegram_api_url);
        printf("  telegram_chat_id   = %s\n", g_cfg.telegram_chat_id[0] ? g_cfg.telegram_chat_id : "(not set)");
        printf("  telegram_token     = %s\n", g_cfg.telegram_token[0]   ? "***"                  : "(not set)");
    }
    printf("\n");
}
