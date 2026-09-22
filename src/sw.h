/* sw — switch your terminal into another operating system's shell profile. */
#ifndef SW_H
#define SW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#define SW_VERSION "1.0.0"
#define SW_PATH 4096
#define SW_DEFAULT_PROFILE "debian13"
#define SW_GUEST_DIR "/var/lib/sw" /* where a container sees its profile directory */

#define FMT(a, b) __attribute__((format(printf, a, b)))

/* ---- util.c ---------------------------------------------------------- */

extern const char *sw_prog;

void die(const char *fmt, ...) FMT(1, 2) __attribute__((noreturn));
void sw_error(const char *fmt, ...) FMT(1, 2);
void sw_warn(const char *fmt, ...) FMT(1, 2);
void note(const char *fmt, ...) FMT(1, 2);

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...) FMT(1, 2);

/* growable string */
typedef struct { char *s; size_t len, cap; } sb_t;
void sb_addn(sb_t *b, const char *s, size_t n);
void sb_add(sb_t *b, const char *s);
void sb_addf(sb_t *b, const char *fmt, ...) FMT(2, 3);
void sb_add_sq(sb_t *b, const char *s); /* append s single-quoted for sh */
const char *sb_str(const sb_t *b);
void sb_free(sb_t *b);

/* growable NULL-terminated string vector (argv, envp, lists) */
typedef struct { char **v; size_t n, cap; } av_t;
void av_push(av_t *a, const char *s);
void av_pushf(av_t *a, const char *fmt, ...) FMT(2, 3);
long av_find(const av_t *a, const char *s);
void av_remove(av_t *a, size_t i);
void av_free(av_t *a);

const char *host_home(void);
const char *host_user(void);
const char *sw_dir(void);
void profile_dir(const char *name, char *out, size_t n);
int mkdir_p(const char *path);
int rm_rf(const char *path);
bool is_dir(const char *path);
bool path_exists(const char *path);
bool path_under(const char *path, const char *dir);
bool have_cmd(const char *name);
int expand_path(const char *in, char *out, size_t n);
void write_file(const char *path, const char *content);
int run_wait(char *const argv[], const char *stdin_path, bool quiet);
int run_capture(char *const argv[], char *out, size_t n);
bool is_tty(void);
bool confirm(const char *question);
unsigned long long fnv1a(const char *s, unsigned long long h);

/* ---- os.c ------------------------------------------------------------ */

typedef enum { BACKEND_CONTAINER, BACKEND_NATIVE } backend_t;
typedef enum { PKG_APT, PKG_DNF, PKG_PACMAN, PKG_APK, PKG_BREW } pkg_t;

typedef struct {
    const char *id;         /* what profiles store: "debian13" */
    const char *pretty;     /* "Debian GNU/Linux 13 (trixie)" */
    backend_t backend;
    pkg_t pkg;              /* where extra tools come from */
    const char *image;      /* container base image */
    const char *platform;   /* forced container platform, or NULL for native */
    const char *shell;      /* the OS's standard shell */
    const char *hostname;   /* default hostname inside the container */
    const char *base_desc;  /* what the base toolset is */
    const char *base_setup; /* shell run at image build to install the base toolset */
} os_def;

const os_def *os_find(const char *id);
const os_def *os_list(size_t *n);
bool os_available(const os_def *os);
const char *pkg_name(pkg_t pkg);

/* ---- profile.c ------------------------------------------------------- */

typedef struct {
    char name[64];
    char os[64];
    char shell[256];    /* empty: the OS's standard shell */
    char hostname[256]; /* empty: the OS default */
    char home[SW_PATH]; /* empty: real home; else an always-on -l, unexpanded */
    char arch[32];      /* empty: native; "amd64" or "arm64" */
    av_t mounts;        /* host directories shared with a container */
    av_t tools;         /* packages (Linux) or Homebrew formulae (macOS) */
} profile_t;

void sw_init(void);
bool valid_name(const char *name);
bool profile_exists(const char *name);
bool profile_load(const char *name, profile_t *p);
void profile_save(const profile_t *p);
void profile_free(profile_t *p);
bool default_profile(char *out, size_t n);
int cmd_profile(int argc, char **argv);
int cmd_inspect(int argc, char **argv);
int cmd_os(int argc, char **argv);

/* ---- launch.c -------------------------------------------------------- */

typedef enum { IMG_UNKNOWN, IMG_MISSING, IMG_STALE, IMG_READY } img_state;

const os_def *profile_os(const profile_t *p);
const char *profile_shell(const profile_t *p, const os_def *os);
const char *profile_platform(const profile_t *p, const os_def *os);
void image_name(const profile_t *p, char *out, size_t n);
bool docker_running(void);
void ensure_docker(void);
int build_image(const profile_t *p, bool force, bool pull);
img_state image_state(const profile_t *p);
const char *brew_prefix(void);
bool brew_formula_installed(const char *name);
void native_link_tools(const profile_t *p);
void launch(const profile_t *p, const char *limit, const char *command) __attribute__((noreturn));

#endif
