#include "sw.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *RESERVED[] = {"profile", "profiles", "inspect", "ins", "exit",
                                 "help", "os", "version", NULL};

bool valid_name(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n > 63 || !(islower((unsigned char)s[0]) || isdigit((unsigned char)s[0])))
        return false;
    for (size_t i = 1; i < n; i++)
        if (!(islower((unsigned char)s[i]) || isdigit((unsigned char)s[i]) || strchr("._-", s[i])))
            return false;
    for (int i = 0; RESERVED[i]; i++)
        if (!strcmp(s, RESERVED[i]))
            return false;
    return true;
}

static bool valid_tool(const char *s)
{
    if (!*s || *s == '-')
        return false;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && !strchr("._+:=@/-", *s))
            return false;
    return true;
}

static void profile_file(const char *name, char *out, size_t n)
{
    char dir[SW_PATH];
    profile_dir(name, dir, sizeof dir);
    snprintf(out, n, "%s/profile", dir);
}

bool profile_exists(const char *name)
{
    char path[SW_PATH];
    profile_file(name, path, sizeof path);
    return path_exists(path);
}

bool profile_load(const char *name, profile_t *p)
{
    memset(p, 0, sizeof *p);
    char path[SW_PATH];
    profile_file(name, path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    snprintf(p->name, sizeof p->name, "%s", name);

    char line[SW_PATH + 64];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *k = line;
        while (*k == ' ' || *k == '\t')
            k++;
        char *eq = strchr(k, '=');
        if (*k == '#' || !eq)
            continue;
        *eq = '\0';
        const char *v = eq + 1;
        if (!strcmp(k, "os"))
            snprintf(p->os, sizeof p->os, "%s", v);
        else if (!strcmp(k, "shell"))
            snprintf(p->shell, sizeof p->shell, "%s", v);
        else if (!strcmp(k, "hostname"))
            snprintf(p->hostname, sizeof p->hostname, "%s", v);
        else if (!strcmp(k, "home"))
            snprintf(p->home, sizeof p->home, "%s", v);
        else if (!strcmp(k, "arch"))
            snprintf(p->arch, sizeof p->arch, "%s", v);
        else if (!strcmp(k, "mount") && *v)
            av_push(&p->mounts, v);
        else if (!strcmp(k, "tool") && *v)
            av_push(&p->tools, v);
    }
    fclose(f);
    return true;
}

void profile_save(const profile_t *p)
{
    char dir[SW_PATH], path[SW_PATH], tmp[SW_PATH];
    profile_dir(p->name, dir, sizeof dir);
    if (mkdir_p(dir))
        die("cannot create %s: %s", dir, strerror(errno));
    profile_file(p->name, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    sb_t b = {0};
    sb_addf(&b, "# sw profile '%s'. Change it with 'sw profile set' and 'sw inspect %s tool'.\n",
            p->name, p->name);
    sb_addf(&b, "os=%s\nshell=%s\nhostname=%s\nhome=%s\narch=%s\n",
            p->os, p->shell, p->hostname, p->home, p->arch);
    for (size_t i = 0; i < p->mounts.n; i++)
        sb_addf(&b, "mount=%s\n", p->mounts.v[i]);
    for (size_t i = 0; i < p->tools.n; i++)
        sb_addf(&b, "tool=%s\n", p->tools.v[i]);
    write_file(tmp, sb_str(&b));
    sb_free(&b);
    if (rename(tmp, path))
        die("cannot write %s: %s", path, strerror(errno));
}

void profile_free(profile_t *p)
{
    av_free(&p->mounts);
    av_free(&p->tools);
}

static void load_or_die(const char *name, profile_t *p)
{
    if (!profile_load(name, p))
        die("no profile named '%s' (see: %s profile list)", name, sw_prog);
}

/* ---- global configuration -------------------------------------------- */

static void config_path(char *out, size_t n) { snprintf(out, n, "%s/config", sw_dir()); }

bool default_profile(char *out, size_t n)
{
    char path[SW_PATH], line[512];
    out[0] = '\0';
    config_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strncmp(line, "default=", 8))
            snprintf(out, n, "%s", line + 8);
    }
    fclose(f);
    return out[0] != '\0';
}

static void set_default(const char *name)
{
    char path[SW_PATH];
    config_path(path, sizeof path);
    sb_t b = {0};
    sb_addf(&b, "# sw configuration\ndefault=%s\n", name);
    write_file(path, sb_str(&b));
    sb_free(&b);
}

static bool is_default(const char *name)
{
    char def[128];
    return default_profile(def, sizeof def) && !strcmp(def, name);
}

static void default_mounts(av_t *mounts)
{
#ifdef __APPLE__
    const char *dirs[] = {"/Users", "/Volumes", NULL};
#else
    const char *dirs[] = {"/home", "/media", "/mnt", NULL};
#endif
    for (int i = 0; dirs[i]; i++)
        if (is_dir(dirs[i]))
            av_push(mounts, dirs[i]);
}

/* First run: create the example Debian 13 profile and make it the default. */
void sw_init(void)
{
    char cfg[SW_PATH], dir[SW_PATH];
    config_path(cfg, sizeof cfg);
    if (path_exists(cfg))
        return;
    snprintf(dir, sizeof dir, "%s/profiles", sw_dir());
    if (mkdir_p(dir))
        die("cannot create %s: %s", dir, strerror(errno));
    if (!profile_exists(SW_DEFAULT_PROFILE)) {
        profile_t p;
        memset(&p, 0, sizeof p);
        snprintf(p.name, sizeof p.name, "%s", SW_DEFAULT_PROFILE);
        snprintf(p.os, sizeof p.os, "%s", SW_DEFAULT_PROFILE);
        default_mounts(&p.mounts);
        profile_save(&p);
        profile_free(&p);
    }
    set_default(SW_DEFAULT_PROFILE);
}

/* ---- sw os ----------------------------------------------------------- */

int cmd_os(int argc, char **argv)
{
    (void)argv;
    if (argc > 1 || (argc == 1 && strcmp(argv[0], "list")))
        die("usage: %s os [list]", sw_prog);
    size_t n;
    const os_def *oses = os_list(&n);
    printf("%-13s %-36s %-10s %s\n", "OS", "SYSTEM", "SHELL", "TOOLS FROM");
    for (size_t i = 0; i < n; i++) {
        if (!os_available(&oses[i]))
            continue;
        printf("%-13s %-36s %-10s %s\n", oses[i].id, oses[i].pretty, oses[i].shell,
               pkg_name(oses[i].pkg));
    }
    return 0;
}

static const os_def *choose_os(void)
{
    size_t n;
    const os_def *oses = os_list(&n);
    if (!isatty(0))
        die("missing OS (see: %s os)", sw_prog);
    printf("Which OS should the profile be?\n");
    for (size_t i = 0; i < n; i++)
        if (os_available(&oses[i]))
            printf("  %zu) %-12s %s\n", i + 1, oses[i].id, oses[i].pretty);
    printf("OS [1]: ");
    fflush(stdout);
    char buf[128];
    if (!fgets(buf, sizeof buf, stdin))
        die("no OS chosen");
    buf[strcspn(buf, "\r\n")] = '\0';
    if (!buf[0])
        return &oses[0];
    char *end;
    long k = strtol(buf, &end, 10);
    if (!*end && k >= 1 && (size_t)k <= n)
        return &oses[k - 1];
    const os_def *os = os_find(buf);
    if (!os)
        die("unknown OS '%s' (see: %s os)", buf, sw_prog);
    return os;
}

static const char *norm_arch(const char *a)
{
    if (!*a)
        return "";
    if (!strcmp(a, "amd64") || !strcmp(a, "x86_64") || !strcmp(a, "x86-64"))
        return "amd64";
    if (!strcmp(a, "arm64") || !strcmp(a, "aarch64"))
        return "arm64";
    die("unknown architecture '%s' (use amd64 or arm64)", a);
}

static void check_home(const char *home)
{
    char tmp[SW_PATH];
    if (*home && expand_path(home, tmp, sizeof tmp))
        exit(1);
}

/* Validate one tool for a profile's OS; false (after a message) if it's not usable. */
static bool tool_ok(const os_def *os, const char *t)
{
    if (!valid_tool(t)) {
        sw_error("'%s' is not a valid package name", t);
        return false;
    }
    if (os->backend == BACKEND_NATIVE) {
        if (strcmp(t, "@all") && !brew_formula_installed(t)) {
            sw_error("'%s' is not an installed Homebrew formula (install it with: brew install %s)",
                     t, t);
            return false;
        }
    } else if (t[0] == '@') {
        sw_error("'%s' only works for macOS profiles; name %s packages instead", t,
                 pkg_name(os->pkg));
        return false;
    }
    return true;
}

/* ---- sw profile ... -------------------------------------------------- */

static void profile_usage(FILE *f)
{
    const char *s = sw_prog;
    fprintf(f,
            "usage: %s profile <command> [args]\n"
            "\n"
            "  add NAME [OS] [TOOL...]   create a profile (asks for the OS if omitted; see '%s os')\n"
            "      -s, --shell SHELL     shell to run (default: the OS's standard shell)\n"
            "      -H, --hostname NAME   hostname the shell sees (Linux profiles)\n"
            "      -l, --home DIR        always limit the profile to DIR as home, like '%s -l DIR'\n"
            "      -a, --arch ARCH       amd64 or arm64 (Linux profiles; default: this Mac's)\n"
            "      -m, --mount DIR       share this host directory (repeatable; default /Users /Volumes)\n"
            "          --no-mounts       share nothing except the -l home\n"
            "  rm NAME [-f]              delete a profile, its image, history and custom.sh\n"
            "  list                      list profiles (* marks the default)\n"
            "  inspect NAME              show a profile (also: ins)\n"
            "  default [NAME]            show or set the profile a plain '%s' switches to\n"
            "  set NAME KEY [VALUE]      change os, shell, hostname, home, arch or mounts\n"
            "                            (no VALUE resets it; mounts take DIR:DIR... or 'none')\n"
            "  build NAME [--pull]       rebuild a Linux profile's image now\n"
            "  help                      show this help\n"
            "\n"
            "Tools are managed with: %s inspect NAME tool [add|rm TOOL...]\n",
            s, s, s, s, s);
}

static int profile_add(int argc, char **argv)
{
    profile_t p;
    memset(&p, 0, sizeof p);
    const char *name = NULL, *os_id = NULL;
    av_t tools = {0}, mounts = {0};
    bool no_mounts = false;

    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        bool has_val = i + 1 < argc;
#define VALUE() (has_val ? argv[++i] : (die("option %s needs a value", a), ""))
        if (!strcmp(a, "-s") || !strcmp(a, "--shell"))
            snprintf(p.shell, sizeof p.shell, "%s", VALUE());
        else if (!strcmp(a, "-H") || !strcmp(a, "--hostname"))
            snprintf(p.hostname, sizeof p.hostname, "%s", VALUE());
        else if (!strcmp(a, "-l") || !strcmp(a, "--home"))
            snprintf(p.home, sizeof p.home, "%s", VALUE());
        else if (!strcmp(a, "-a") || !strcmp(a, "--arch"))
            snprintf(p.arch, sizeof p.arch, "%s", norm_arch(VALUE()));
        else if (!strcmp(a, "-m") || !strcmp(a, "--mount"))
            av_push(&mounts, VALUE());
        else if (!strcmp(a, "--no-mounts"))
            no_mounts = true;
        else if (a[0] == '-')
            die("unknown option '%s' (see: %s profile help)", a, sw_prog);
        else if (!name)
            name = a;
        else if (!os_id)
            os_id = a;
        else
            av_push(&tools, a);
#undef VALUE
    }

    if (!name)
        die("usage: %s profile add NAME [OS] [TOOL...]", sw_prog);
    if (!valid_name(name))
        die("invalid profile name '%s': use lowercase letters, digits, '.', '_' and '-', "
            "and not a sw command word", name);
    if (profile_exists(name))
        die("profile '%s' already exists (see: %s inspect %s)", name, sw_prog, name);

    const os_def *os = os_id ? os_find(os_id) : choose_os();
    if (!os)
        die("unknown OS '%s' (see: %s os)", os_id, sw_prog);
    if (!os_available(os))
        die("%s profiles can only be used on that system", os->pretty);
    if (os->backend == BACKEND_NATIVE && (p.arch[0] || p.hostname[0] || mounts.n || no_mounts))
        die("--arch, --hostname and --mount only apply to Linux profiles");
    check_home(p.home);

    snprintf(p.name, sizeof p.name, "%s", name);
    snprintf(p.os, sizeof p.os, "%s", os->id);
    for (size_t i = 0; i < tools.n; i++) {
        if (!tool_ok(os, tools.v[i]))
            exit(1);
        if (av_find(&p.tools, tools.v[i]) < 0)
            av_push(&p.tools, tools.v[i]);
    }
    if (os->backend == BACKEND_CONTAINER && !no_mounts) {
        if (!mounts.n)
            default_mounts(&p.mounts);
        for (size_t i = 0; i < mounts.n; i++) {
            char m[SW_PATH];
            if (expand_path(mounts.v[i], m, sizeof m))
                exit(1);
            if (!is_dir(m))
                die("%s is not a directory", m);
            av_push(&p.mounts, m);
        }
    }

    profile_save(&p);
    if (os->backend == BACKEND_NATIVE)
        native_link_tools(&p);
    printf("Created profile '%s' (%s).\n", name, os->pretty);
    char def[128];
    if (!default_profile(def, sizeof def) || !profile_exists(def)) {
        set_default(name);
        printf("It is now the default profile.\n");
    }
    printf("Switch to it with: %s %s\n", sw_prog, name);
    if (os->backend == BACKEND_CONTAINER)
        printf("Its image is built on the first switch, or now with: %s profile build %s\n",
               sw_prog, name);
    profile_free(&p);
    av_free(&tools);
    av_free(&mounts);
    return 0;
}

static int profile_rm(int argc, char **argv)
{
    const char *name = NULL;
    bool force = false;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--force"))
            force = true;
        else if (!name)
            name = argv[i];
        else
            die("usage: %s profile rm NAME [-f]", sw_prog);
    }
    if (!name)
        die("usage: %s profile rm NAME [-f]", sw_prog);

    profile_t p;
    load_or_die(name, &p);
    if (!strcmp(name, SW_DEFAULT_PROFILE))
        die("'%s' is the built-in default profile and cannot be deleted", name);
    if (is_default(name))
        die("'%s' is the default profile and cannot be deleted; make another profile "
            "the default first (%s profile default NAME)", name, sw_prog);
    if (!force) {
        if (!isatty(0))
            die("not deleting '%s' without confirmation; use -f", name);
        char *q = xasprintf("Delete profile '%s' with its image, history and custom.sh?", name);
        bool yes = confirm(q);
        free(q);
        if (!yes) {
            printf("Kept '%s'.\n", name);
            profile_free(&p);
            return 1;
        }
    }

    const os_def *os = os_find(p.os);
    if (os && os->backend == BACKEND_CONTAINER && docker_running()) {
        char img[128];
        image_name(&p, img, sizeof img);
        char *argv_rm[] = {"docker", "image", "rm", "--force", img, NULL};
        run_wait(argv_rm, NULL, true);
    }
    char dir[SW_PATH];
    profile_dir(name, dir, sizeof dir);
    if (rm_rf(dir))
        die("cannot delete %s: %s", dir, strerror(errno));
    printf("Deleted profile '%s'.\n", name);
    profile_free(&p);
    return 0;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void list_names(av_t *names)
{
    char dir[SW_PATH];
    snprintf(dir, sizeof dir, "%s/profiles", sw_dir());
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.' && profile_exists(e->d_name))
            av_push(names, e->d_name);
    closedir(d);
    if (names->n)
        qsort(names->v, names->n, sizeof *names->v, cmp_str);
}

static int profile_list(void)
{
    av_t names = {0};
    list_names(&names);
    if (!names.n) {
        printf("No profiles. Create one with: %s profile add NAME OS\n", sw_prog);
        return 0;
    }
    char def[128];
    default_profile(def, sizeof def);
    printf("  %-16s %-36s %s\n", "PROFILE", "OS", "TOOLS");
    for (size_t i = 0; i < names.n; i++) {
        profile_t p;
        if (!profile_load(names.v[i], &p))
            continue;
        const os_def *os = os_find(p.os);
        char tools[64];
        if (p.tools.n)
            snprintf(tools, sizeof tools, "base + %zu", p.tools.n);
        else
            snprintf(tools, sizeof tools, "base");
        printf("%c %-16s %-36s %s\n", strcmp(def, p.name) ? ' ' : '*', p.name,
               os ? os->pretty : p.os, tools);
        profile_free(&p);
    }
    av_free(&names);
    return 0;
}

static int profile_default_cmd(int argc, char **argv)
{
    if (argc > 1)
        die("usage: %s profile default [NAME]", sw_prog);
    if (argc == 0) {
        char def[128];
        if (default_profile(def, sizeof def))
            printf("%s\n", def);
        else
            printf("(no default profile; set one with: %s profile default NAME)\n", sw_prog);
        return 0;
    }
    if (!profile_exists(argv[0]))
        die("no profile named '%s' (see: %s profile list)", argv[0], sw_prog);
    set_default(argv[0]);
    printf("Default profile is now '%s'.\n", argv[0]);
    return 0;
}

static int profile_set(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
        die("usage: %s profile set NAME os|shell|hostname|home|arch|mounts [VALUE]", sw_prog);
    profile_t p;
    load_or_die(argv[0], &p);
    const char *key = argv[1], *val = argc == 3 ? argv[2] : "";
    const os_def *os = os_find(p.os);

    if (!strcmp(key, "os")) {
        const os_def *nos = os_find(val);
        if (!nos)
            die("unknown OS '%s' (see: %s os)", val, sw_prog);
        if (!os_available(nos))
            die("%s profiles can only be used on that system", nos->pretty);
        if (p.tools.n && (!os || os->pkg != nos->pkg)) {
            printf("Dropped tools that came from %s:", os ? pkg_name(os->pkg) : "the old OS");
            for (size_t i = 0; i < p.tools.n; i++)
                printf(" %s", p.tools.v[i]);
            printf("\n");
            av_free(&p.tools);
        }
        if (nos->backend == BACKEND_NATIVE) {
            p.arch[0] = p.hostname[0] = '\0';
        } else if (os && os->backend == BACKEND_NATIVE && !p.mounts.n) {
            default_mounts(&p.mounts);
        }
        snprintf(p.os, sizeof p.os, "%s", nos->id);
        os = nos;
    } else if (!strcmp(key, "shell")) {
        snprintf(p.shell, sizeof p.shell, "%s", val);
    } else if (!strcmp(key, "hostname")) {
        snprintf(p.hostname, sizeof p.hostname, "%s", val);
    } else if (!strcmp(key, "home")) {
        check_home(val);
        snprintf(p.home, sizeof p.home, "%s", val);
    } else if (!strcmp(key, "arch")) {
        snprintf(p.arch, sizeof p.arch, "%s", norm_arch(val));
    } else if (!strcmp(key, "mounts")) {
        av_free(&p.mounts);
        if (!*val) {
            default_mounts(&p.mounts);
        } else if (strcmp(val, "none")) {
            char *copy = xstrdup(val), *save = NULL;
            for (char *m = strtok_r(copy, ":", &save); m; m = strtok_r(NULL, ":", &save)) {
                char abs[SW_PATH];
                if (expand_path(m, abs, sizeof abs))
                    exit(1);
                if (!is_dir(abs))
                    die("%s is not a directory", abs);
                av_push(&p.mounts, abs);
            }
            free(copy);
        }
    } else {
        die("unknown setting '%s' (os, shell, hostname, home, arch or mounts)", key);
    }

    profile_save(&p);
    printf("Updated '%s': %s = %s\n", p.name, key, *val ? val : "(default)");
    profile_free(&p);
    return 0;
}

static int profile_build(int argc, char **argv)
{
    bool pull = false;
    const char *name = NULL;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--pull"))
            pull = true;
        else if (!name)
            name = argv[i];
        else
            die("usage: %s profile build NAME [--pull]", sw_prog);
    }
    if (!name)
        die("usage: %s profile build NAME [--pull]", sw_prog);
    profile_t p;
    load_or_die(name, &p);
    if (profile_os(&p)->backend != BACKEND_CONTAINER) {
        native_link_tools(&p);
        printf("'%s' runs natively; its tools were relinked.\n", name);
        return 0;
    }
    ensure_docker();
    int rc = build_image(&p, true, pull);
    if (rc)
        die("building '%s' failed", name);
    printf("Image for '%s' is up to date.\n", name);
    profile_free(&p);
    return 0;
}

/* ---- inspect --------------------------------------------------------- */

static void print_tools(const profile_t *p, const os_def *os)
{
    printf("(base)  %s\n", os ? os->base_desc : "unknown OS");
    for (size_t i = 0; i < p->tools.n; i++) {
        if (!strcmp(p->tools.v[i], "@all"))
            printf("@all    every Homebrew formula in %s\n", brew_prefix());
        else
            printf("%s\n", p->tools.v[i]);
    }
}

static void print_profile(const profile_t *p)
{
    const os_def *os = os_find(p->os);
    char dir[SW_PATH], img[128];
    profile_dir(p->name, dir, sizeof dir);
    image_name(p, img, sizeof img);

    printf("Profile   %s%s\n", p->name, is_default(p->name) ? "  (default)" : "");
    printf("OS        %s  [%s]\n", os ? os->pretty : "unknown", p->os);
    if (!os) {
        printf("Files     %s\n", dir);
        return;
    }
    printf("Shell     %s%s\n", profile_shell(p, os), p->shell[0] ? "" : "  (the OS's standard)");

    if (os->backend == BACKEND_CONTAINER) {
        const char *plat = profile_platform(p, os);
        printf("Runs as   container from %s%s%s%s\n", os->image, plat ? " (" : "",
               plat ? plat : "", plat ? ")" : "");
        printf("Hostname  %s\n", p->hostname[0] ? p->hostname : os->hostname);
        const char *state = "unknown (Docker is not running)";
        switch (image_state(p)) {
        case IMG_READY: state = "built, up to date"; break;
        case IMG_STALE: state = "outdated, rebuilt on the next switch"; break;
        case IMG_MISSING: state = "not built yet, built on the first switch"; break;
        case IMG_UNKNOWN: break;
        }
        printf("Image     %s: %s\n", img, state);
    } else {
        printf("Runs as   this Mac, with a PATH of only the tools below\n");
    }

    if (p->home[0])
        printf("Home      %s  (always limited, like -l)\n", p->home);
    else
        printf("Home      your real home (limit it with: %s -l DIR %s)\n", sw_prog, p->name);
    if (os->backend == BACKEND_CONTAINER) {
        printf("Shared   ");
        if (!p->mounts.n)
            printf(" nothing");
        for (size_t i = 0; i < p->mounts.n; i++)
            printf(" %s", p->mounts.v[i]);
        printf("\n");
    }
    printf("Tools     from %s:\n", pkg_name(os->pkg));
    printf("          (base)  %s\n", os->base_desc);
    for (size_t i = 0; i < p->tools.n; i++)
        printf("          %s\n", p->tools.v[i]);
    printf("Files     %s\n", dir);
}

static int tool_add(profile_t *p, const os_def *os, int argc, char **argv)
{
    av_t added = {0};
    for (int i = 0; i < argc; i++) {
        if (!tool_ok(os, argv[i]))
            exit(1);
        if (av_find(&p->tools, argv[i]) >= 0) {
            note("'%s' is already in profile '%s'", argv[i], p->name);
            continue;
        }
        av_push(&p->tools, argv[i]);
        av_push(&added, argv[i]);
    }
    if (!added.n)
        return 0;

    if (os->backend == BACKEND_NATIVE) {
        profile_save(p);
        native_link_tools(p);
    } else {
        ensure_docker();
        if (build_image(p, false, false)) {
            sw_error("installing failed; check the %s package names (profile '%s' is unchanged)",
                     pkg_name(os->pkg), p->name);
            return 1;
        }
        profile_save(p);
    }
    printf("Added to '%s':", p->name);
    for (size_t i = 0; i < added.n; i++)
        printf(" %s", added.v[i]);
    printf("\n");
    av_free(&added);
    return 0;
}

static int tool_rm(profile_t *p, const os_def *os, int argc, char **argv)
{
    av_t removed = {0};
    for (int i = 0; i < argc; i++) {
        long k = av_find(&p->tools, argv[i]);
        if (k < 0) {
            sw_warn("'%s' is not in profile '%s'", argv[i], p->name);
            continue;
        }
        av_remove(&p->tools, (size_t)k);
        av_push(&removed, argv[i]);
    }
    if (!removed.n)
        return 1;
    profile_save(p);
    if (os->backend == BACKEND_NATIVE)
        native_link_tools(p);
    else if (docker_running() && build_image(p, false, false))
        sw_warn("rebuilding the image failed; it is retried on the next switch");
    printf("Removed from '%s':", p->name);
    for (size_t i = 0; i < removed.n; i++)
        printf(" %s", removed.v[i]);
    printf("\n");
    av_free(&removed);
    return 0;
}

int cmd_inspect(int argc, char **argv)
{
    if (argc < 1)
        die("usage: %s inspect NAME [tool [add|rm TOOL...]]", sw_prog);
    profile_t p;
    load_or_die(argv[0], &p);
    int rc = 0;

    if (argc == 1) {
        print_profile(&p);
    } else if (strcmp(argv[1], "tool") && strcmp(argv[1], "tools")) {
        die("usage: %s inspect NAME [tool [add|rm TOOL...]]", sw_prog);
    } else if (argc == 2) {
        print_tools(&p, os_find(p.os));
    } else {
        const char *op = argv[2];
        bool add = !strcmp(op, "add");
        bool rm = !strcmp(op, "rm") || !strcmp(op, "remove");
        if (!add && !rm)
            die("unknown tool command '%s' (add, rm or remove)", op);
        if (argc == 3)
            die("usage: %s inspect %s tool %s TOOL...", sw_prog, p.name, op);
        const os_def *os = profile_os(&p);
        rc = add ? tool_add(&p, os, argc - 3, argv + 3) : tool_rm(&p, os, argc - 3, argv + 3);
    }
    profile_free(&p);
    return rc;
}

int cmd_profile(int argc, char **argv)
{
    const char *sub = argc ? argv[0] : "help";
    if (!strcmp(sub, "add"))
        return profile_add(argc - 1, argv + 1);
    if (!strcmp(sub, "rm") || !strcmp(sub, "remove"))
        return profile_rm(argc - 1, argv + 1);
    if (!strcmp(sub, "list") || !strcmp(sub, "ls"))
        return profile_list();
    if (!strcmp(sub, "inspect") || !strcmp(sub, "ins"))
        return cmd_inspect(argc - 1, argv + 1);
    if (!strcmp(sub, "default"))
        return profile_default_cmd(argc - 1, argv + 1);
    if (!strcmp(sub, "set"))
        return profile_set(argc - 1, argv + 1);
    if (!strcmp(sub, "build"))
        return profile_build(argc - 1, argv + 1);
    if (!strcmp(sub, "help") || !strcmp(sub, "-h") || !strcmp(sub, "--help")) {
        profile_usage(stdout);
        return 0;
    }
    profile_usage(stderr);
    return 2;
}
