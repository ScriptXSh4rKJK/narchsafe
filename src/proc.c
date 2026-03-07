#include "proc.h"
#include "log.h"

char *const SAFE_ENV[] = {
    "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
    "HOME=/root",
    "LANG=C",
    NULL
};

void child_init(void) {
    // Close the log fd so it is not inherited across fork
    if (g_logfile) { fclose(g_logfile); g_logfile = NULL; }
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
    signal(SIGINT,  SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
}

int wait_child(pid_t pid, const char *name) {
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        LOGE("waitpid(%s): %s", name, strerror(errno));
        return -1;
    }
    if (WIFSIGNALED(status)) {
        LOGW("%s killed by signal %d", name, WTERMSIG(status));
        return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

int run(const char *path, char *const argv[]) {
    if (access(path, X_OK) != 0) { LOGE("%s: not found or not executable", path); return -1; }
    pid_t pid = fork();
    if (pid < 0) { LOGE("fork: %s", strerror(errno)); return -1; }
    if (pid == 0) {
        child_init();
        execve(path, argv, SAFE_ENV);
        static const char e[] = "execve failed\n";
        ssize_t wr = write(STDERR_FILENO, e, sizeof(e) - 1);
        (void)wr;
        _exit(127);
    }
    return wait_child(pid, path);
}

int run_to_file(const char *path, char *const argv[], const char *outpath) {
    if (access(path, X_OK) != 0) { LOGE("%s: not found", path); return -1; }
    pid_t pid = fork();
    if (pid < 0) { LOGE("fork: %s", strerror(errno)); return -1; }
    if (pid == 0) {
        child_init();
        int fd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) _exit(1);
        if (dup2(fd, STDOUT_FILENO) < 0) _exit(1);
        close(fd);
        execve(path, argv, SAFE_ENV);
        _exit(127);
    }
    return wait_child(pid, path);
}

int run_tee(const char *path, char *const argv[], FILE *logfp) {
    if (access(path, X_OK) != 0) { LOGE("%s: not found", path); return -1; }
    int pfd[2];
    if (pipe(pfd) != 0) { LOGE("pipe: %s", strerror(errno)); return -1; }
    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]); close(pfd[1]);
        LOGE("fork: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        close(pfd[0]);
        if (dup2(pfd[1], STDOUT_FILENO) < 0) _exit(1);
        if (dup2(pfd[1], STDERR_FILENO) < 0) _exit(1);
        close(pfd[1]);
        child_init();
        execve(path, argv, SAFE_ENV);
        _exit(127);
    }
    close(pfd[1]);
    char buf[4096];
    ssize_t n;
    while ((n = read(pfd[0], buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, (size_t)n, stdout);
        fflush(stdout);
        if (logfp) { fwrite(buf, 1, (size_t)n, logfp); fflush(logfp); }
    }
    close(pfd[0]);
    return wait_child(pid, path);
}

int run_capture(const char *path, char *const argv[], char *buf, size_t bufsz) {
    if (!buf || bufsz == 0) return -1;
    buf[0] = '\0';
    if (access(path, X_OK) != 0) { LOGE("%s: not found", path); return -1; }
    int pfd[2];
    if (pipe(pfd) != 0) { LOGE("pipe: %s", strerror(errno)); return -1; }
    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]); close(pfd[1]);
        LOGE("fork: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        close(pfd[0]);
        if (dup2(pfd[1], STDOUT_FILENO) < 0) _exit(1);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        close(pfd[1]);
        child_init();
        execve(path, argv, SAFE_ENV);
        _exit(127);
    }
    close(pfd[1]);
    size_t total = 0;
    ssize_t n;
    while ((n = read(pfd[0], buf + total, bufsz - 1 - total)) > 0)
        total += (size_t)n;
    buf[total] = '\0';
    // Drop trailing newline so callers can use the string directly
    if (total > 0 && buf[total - 1] == '\n') buf[--total] = '\0';
    close(pfd[0]);
    return wait_child(pid, path);
}

int run_mut(const char *path, char *const argv[]) {
    if (g_dry_run) {
        printf("  [DRY-RUN] would run:");
        for (int i = 0; argv[i]; i++) printf(" %s", argv[i]);
        printf("\n");
        return 0;
    }
    return run(path, argv);
}

int run_tee_mut(const char *path, char *const argv[], FILE *logfp) {
    if (g_dry_run) {
        printf("  [DRY-RUN] would run:");
        for (int i = 0; argv[i]; i++) printf(" %s", argv[i]);
        printf("\n");
        if (logfp) fprintf(logfp, "[DRY-RUN] skipped\n");
        return 0;
    }
    return run_tee(path, argv, logfp);
}
