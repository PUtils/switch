#include "sw.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *f)
{
    const char *s = sw_prog;
    fprintf(f,
            "usage: %s [-l DIR] [PROFILE] [-c COMMAND]\n"
            "       %s exit\n"
            "       %s profile add|rm|list|inspect|default|set|build|help ...\n"
            "       %s inspect PROFILE [tool [add|rm TOOL...]]\n"
            "       %s os\n"
            "\n"
            "Switch this terminal into a separate shell profile that looks and behaves like\n"
            "another system (Debian 13 by default) while working on your own files.\n"
            "\n"
            "  (no arguments)         switch to the default profile\n"
            "  PROFILE                switch to PROFILE\n"
            "  -l, --limit DIR        make DIR the home directory and the only thing visible\n"
            "  -c COMMAND             run COMMAND inside the profile instead of a shell\n"
            "  exit                   leave the profile (inside it; plain 'exit' works too)\n"
            "  profile ...            manage profiles (see: %s profile help)\n"
            "  inspect, ins PROFILE   show a profile; add 'tool' to list or change its tools\n"
            "  os                     list the systems a profile can be\n"
            "  -h, --help             show this help\n"
            "  -V, --version          show the version\n"
            "\n"
            "Full manual: man sw\n",
            s, s, s, s, s, s);
}

static int cmd_exit(void)
{
    const char *active = getenv("SW_PROFILE");
    if (!active || !*active)
        die("not inside a profile, nothing to exit");
    /* Reached only when the shell's own sw function is missing: end the shell. */
    kill(getppid(), SIGHUP);
    return 0;
}

int main(int argc, char **argv)
{
    const char *base = strrchr(argv[0], '/');
    sw_prog = base ? base + 1 : argv[0];

    if (argc >= 2) {
        const char *c = argv[1];
        if (!strcmp(c, "help") || !strcmp(c, "-h") || !strcmp(c, "--help")) {
            usage(stdout);
            return 0;
        }
        if (!strcmp(c, "-V") || !strcmp(c, "--version") || !strcmp(c, "version")) {
            printf("sw %s\n", SW_VERSION);
            return 0;
        }
        if (!strcmp(c, "exit"))
            return cmd_exit();
        sw_init();
        if (!strcmp(c, "profile") || !strcmp(c, "profiles"))
            return cmd_profile(argc - 2, argv + 2);
        if (!strcmp(c, "inspect") || !strcmp(c, "ins"))
            return cmd_inspect(argc - 2, argv + 2);
        if (!strcmp(c, "os"))
            return cmd_os(argc - 2, argv + 2);
    }
    sw_init();

    const char *name = NULL, *limit = NULL, *command = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-l") || !strcmp(a, "--limit")) {
            if (++i >= argc)
                die("%s needs a directory", a);
            limit = argv[i];
        } else if (!strncmp(a, "--limit=", 8)) {
            limit = a + 8;
        } else if (!strncmp(a, "-l", 2) && a[2]) {
            limit = a + 2;
        } else if (!strcmp(a, "-c")) {
            if (++i >= argc)
                die("-c needs a command");
            command = argv[i];
        } else if (a[0] == '-') {
            die("unknown option '%s' (see: %s help)", a, sw_prog);
        } else if (!name) {
            name = a;
        } else {
            die("unexpected argument '%s' (see: %s help)", a, sw_prog);
        }
    }

    char def[128];
    if (!name) {
        if (!default_profile(def, sizeof def))
            die("no default profile; pick one with: %s profile default NAME", sw_prog);
        name = def;
    }
    profile_t p;
    if (!profile_load(name, &p))
        die("no profile named '%s' (see: %s profile list)", name, sw_prog);

    char lim[SW_PATH];
    if (limit && expand_path(limit, lim, sizeof lim))
        return 1;
    launch(&p, limit ? lim : NULL, command);
}
