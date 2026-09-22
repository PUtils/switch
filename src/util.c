#include "sw.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

const char *sw_prog = "sw";

/* ---- messages -------------------------------------------------------- */

static void vmsg(const char *kind, const char *fmt, va_list ap)
{
    fflush(stdout);
    fprintf(stderr, "%s: %s", sw_prog, kind);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg("", fmt, ap);
    va_end(ap);
    exit(1);
}

void sw_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg("", fmt, ap);
    va_end(ap);
}

void sw_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg("warning: ", fmt, ap);
    va_end(ap);
}

void note(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vmsg("", fmt, ap);
    va_end(ap);
}

/* ---- memory ---------------------------------------------------------- */

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

static char *xvasprintf(const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    char *s = xmalloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return s;
}

char *xasprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *s = xvasprintf(fmt, ap);
    va_end(ap);
    return s;
}

/* ---- strings --------------------------------------------------------- */

void sb_addn(sb_t *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n + 1)
            cap *= 2;
        b->s = xrealloc(b->s, cap);
        b->cap = cap;
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = '\0';
}

void sb_add(sb_t *b, const char *s) { sb_addn(b, s, strlen(s)); }

void sb_addf(sb_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *s = xvasprintf(fmt, ap);
    va_end(ap);
    sb_add(b, s);
    free(s);
}

void sb_add_sq(sb_t *b, const char *s)
{
    sb_add(b, "'");
    for (; *s; s++) {
        if (*s == '\'')
            sb_add(b, "'\\''");
        else
            sb_addn(b, s, 1);
    }
    sb_add(b, "'");
}

const char *sb_str(const sb_t *b) { return b->s ? b->s : ""; }

void sb_free(sb_t *b)
{
    free(b->s);
    b->s = NULL;
    b->len = b->cap = 0;
}

void av_push(av_t *a, const char *s)
{
    if (a->n + 2 > a->cap) {
        a->cap = a->cap ? a->cap * 2 : 16;
        a->v = xrealloc(a->v, a->cap * sizeof *a->v);
    }
    a->v[a->n++] = xstrdup(s);
    a->v[a->n] = NULL;
}

void av_pushf(av_t *a, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *s = xvasprintf(fmt, ap);
    va_end(ap);
    av_push(a, s);
    free(s);
}

long av_find(const av_t *a, const char *s)
{
    for (size_t i = 0; i < a->n; i++)
        if (!strcmp(a->v[i], s))
            return (long)i;
    return -1;
}

void av_remove(av_t *a, size_t i)
{
    if (i >= a->n)
        return;
    free(a->v[i]);
    memmove(&a->v[i], &a->v[i + 1], (a->n - i) * sizeof *a->v); /* moves the NULL too */
    a->n--;
}

void av_free(av_t *a)
{
    for (size_t i = 0; i < a->n; i++)
        free(a->v[i]);
    free(a->v);
    a->v = NULL;
    a->n = a->cap = 0;
}

/* ---- host and paths -------------------------------------------------- */

const char *host_home(void)
{
    static char buf[SW_PATH];
    const char *h = getenv("HOME");
    if (h && *h)
        return h;
    struct passwd *pw = getpwuid(getuid());
    snprintf(buf, sizeof buf, "%s", pw ? pw->pw_dir : "/");
    return buf;
}

const char *host_user(void)
{
    static char buf[256];
    if (!buf[0]) {
        struct passwd *pw = getpwuid(getuid());
        const char *u = pw ? pw->pw_name : getenv("USER");
        snprintf(buf, sizeof buf, "%s", u && *u ? u : "user");
    }
    return buf;
}

const char *sw_dir(void)
{
    static char buf[SW_PATH];
    if (!buf[0]) {
        const char *x = getenv("SW_HOME");
        if (x && *x) {
            snprintf(buf, sizeof buf, "%s", x);
        } else if ((x = getenv("XDG_CONFIG_HOME")) && *x) {
            snprintf(buf, sizeof buf, "%s/sw", x);
        } else {
            snprintf(buf, sizeof buf, "%s/.config/sw", host_home());
        }
    }
    return buf;
}

void profile_dir(const char *name, char *out, size_t n)
{
    snprintf(out, n, "%s/profiles/%s", sw_dir(), name);
}

int mkdir_p(const char *path)
{
    char tmp[SW_PATH];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(tmp, 0755) && errno != EEXIST)
            return -1;
        *p = '/';
    }
    if (mkdir(tmp, 0755) && errno != EEXIST)
        return -1;
    return is_dir(tmp) ? 0 : -1;
}

static int rm_one(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st, (void)flag, (void)ftw;
    return remove(path);
}

int rm_rf(const char *path)
{
    struct stat st;
    if (lstat(path, &st))
        return errno == ENOENT ? 0 : -1;
    return nftw(path, rm_one, 16, FTW_DEPTH | FTW_PHYS);
}

bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool path_exists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

bool path_under(const char *path, const char *dir)
{
    size_t n = strlen(dir);
    if (n == 1 && dir[0] == '/')
        return path[0] == '/';
    return strncmp(path, dir, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

bool have_cmd(const char *name)
{
    const char *path = getenv("PATH");
    if (!path)
        return false;
    char buf[SW_PATH];
    for (const char *p = path; *p;) {
        const char *e = strchr(p, ':');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        snprintf(buf, sizeof buf, "%.*s/%s", (int)n, n ? p : ".", name);
        if (access(buf, X_OK) == 0)
            return true;
        if (!e)
            break;
        p = e + 1;
    }
    return false;
}

/* Expand ~, ~user, $VAR and ${VAR}; make the result absolute and resolve it
 * when it exists. Returns 0, or -1 after printing why. */
int expand_path(const char *in, char *out, size_t n)
{
    sb_t b = {0};
    const char *p = in;

    if (p[0] == '~') {
        const char *e = p + 1;
        while (*e && *e != '/')
            e++;
        if (e == p + 1) {
            sb_add(&b, host_home());
        } else {
            char user[256];
            snprintf(user, sizeof user, "%.*s", (int)(e - p - 1), p + 1);
            struct passwd *pw = getpwnam(user);
            if (!pw) {
                sw_error("unknown user '%s' in '%s'", user, in);
                goto fail;
            }
            sb_add(&b, pw->pw_dir);
        }
        p = e;
    }

    while (*p) {
        if (p[0] == '$' && (p[1] == '{' || p[1] == '_' || isalpha((unsigned char)p[1]))) {
            char var[256];
            size_t k = 0;
            const char *q;
            if (p[1] == '{') {
                for (q = p + 2; *q && *q != '}'; q++)
                    if (k < sizeof var - 1)
                        var[k++] = *q;
                if (*q != '}') {
                    sw_error("unterminated ${ in '%s'", in);
                    goto fail;
                }
                q++;
            } else {
                for (q = p + 1; *q == '_' || isalnum((unsigned char)*q); q++)
                    if (k < sizeof var - 1)
                        var[k++] = *q;
            }
            var[k] = '\0';
            const char *v = getenv(var);
            if (!v) {
                sw_error("variable $%s in '%s' is not set", var, in);
                goto fail;
            }
            sb_add(&b, v);
            p = q;
        } else {
            sb_addn(&b, p++, 1);
        }
    }

    if (!b.len) {
        sw_error("empty path");
        goto fail;
    }

    char abs[SW_PATH], real[PATH_MAX];
    if (b.s[0] == '/') {
        snprintf(abs, sizeof abs, "%s", b.s);
    } else {
        char cwd[SW_PATH];
        if (!getcwd(cwd, sizeof cwd))
            snprintf(cwd, sizeof cwd, "/");
        snprintf(abs, sizeof abs, "%s/%s", cwd, b.s);
    }
    size_t len = strlen(abs);
    while (len > 1 && abs[len - 1] == '/')
        abs[--len] = '\0';
    snprintf(out, n, "%s", realpath(abs, real) ? real : abs);
    sb_free(&b);
    return 0;

fail:
    sb_free(&b);
    return -1;
}

void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f || fputs(content, f) == EOF || fclose(f) != 0)
        die("cannot write %s: %s", path, strerror(errno));
}

/* ---- processes ------------------------------------------------------- */

static int wait_child(pid_t pid)
{
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR)
            return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

int run_wait(char *const argv[], const char *stdin_path, bool quiet)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        if (stdin_path) {
            int fd = open(stdin_path, O_RDONLY);
            if (fd < 0)
                _exit(127);
            dup2(fd, 0);
            close(fd);
        }
        if (quiet) {
            int fd = open("/dev/null", O_WRONLY);
            dup2(fd, 1);
            dup2(fd, 2);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    return wait_child(pid);
}

/* Run argv, capture its stdout (trailing newlines dropped), discard stderr. */
int run_capture(char *const argv[], char *out, size_t n)
{
    int fds[2];
    out[0] = '\0';
    fflush(NULL);
    if (pipe(fds))
        return -1;
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        dup2(null, 0);
        dup2(fds[1], 1);
        dup2(null, 2);
        close(fds[0]);
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    size_t len = 0;
    ssize_t r;
    char sink[512];
    while ((r = read(fds[0], len + 1 < n ? out + len : sink,
                     len + 1 < n ? n - 1 - len : sizeof sink)) != 0) {
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (len + 1 < n)
            len += (size_t)r;
    }
    close(fds[0]);
    out[len] = '\0';
    while (len && (out[len - 1] == '\n' || out[len - 1] == '\r'))
        out[--len] = '\0';
    return wait_child(pid);
}

bool is_tty(void) { return isatty(0) && isatty(1); }

bool confirm(const char *question)
{
    if (!isatty(0))
        return false;
    printf("%s [y/N] ", question);
    fflush(stdout);
    char buf[64];
    if (!fgets(buf, sizeof buf, stdin))
        return false;
    return buf[0] == 'y' || buf[0] == 'Y';
}

unsigned long long fnv1a(const char *s, unsigned long long h)
{
    if (!h)
        h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}
