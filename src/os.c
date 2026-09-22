#include "sw.h"

#include <string.h>

/* Every package of priority required, important or standard: exactly what a
 * Debian installer puts on a machine with "standard system utilities" ticked. */
#define APT_STANDARD                                                          \
    "apt-get update && DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=5 install -y "    \
    "$(apt-cache dumpavail | awk '/^Package:/{p=$2} "                         \
    "/^Priority: (required|important|standard)$/{print p}' | sort -u) "       \
    "&& apt-get clean"

/* Ubuntu images are minimized (no man pages); undo that, then add the
 * ubuntu-standard set a normal install has. */
#define UBUNTU_STANDARD                                                       \
    "apt-get update && { DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=5 install -y "  \
    "unminimize || true; } && yes | DEBIAN_FRONTEND=noninteractive "          \
    "unminimize && DEBIAN_FRONTEND=noninteractive apt-get -o Acquire::Retries=5 install -y "        \
    "ubuntu-minimal ubuntu-standard && apt-get clean"

#define FEDORA_CORE                                                           \
    "sed -i '/tsflags=nodocs/d' /etc/dnf/dnf.conf 2>/dev/null; "             \
    "dnf -y install @core man-db man-pages less which procps-ng findutils "   \
    "&& dnf clean all"

#define ARCH_BASE                                                             \
    "sed -i '/^NoExtract/d' /etc/pacman.conf && pacman -Syu --noconfirm "     \
    "--needed base man-db man-pages less which && yes | pacman -Scc"

#define ALPINE_BASE "apk add --no-cache mandoc man-pages less"

static const os_def OSES[] = {
    {"debian13", "Debian GNU/Linux 13 (trixie)", BACKEND_CONTAINER, PKG_APT,
     "debian:trixie", NULL, "/bin/bash", "debian",
     "Debian standard system utilities (priority required, important, standard)",
     APT_STANDARD},
    {"debian12", "Debian GNU/Linux 12 (bookworm)", BACKEND_CONTAINER, PKG_APT,
     "debian:bookworm", NULL, "/bin/bash", "debian",
     "Debian standard system utilities (priority required, important, standard)",
     APT_STANDARD},
    {"ubuntu24.04", "Ubuntu 24.04 LTS (Noble Numbat)", BACKEND_CONTAINER, PKG_APT,
     "ubuntu:24.04", NULL, "/bin/bash", "ubuntu",
     "ubuntu-minimal + ubuntu-standard, unminimized", UBUNTU_STANDARD},
    {"ubuntu22.04", "Ubuntu 22.04 LTS (Jammy Jellyfish)", BACKEND_CONTAINER, PKG_APT,
     "ubuntu:22.04", NULL, "/bin/bash", "ubuntu",
     "ubuntu-minimal + ubuntu-standard, unminimized", UBUNTU_STANDARD},
    {"fedora", "Fedora Linux (latest)", BACKEND_CONTAINER, PKG_DNF,
     "fedora:latest", NULL, "/bin/bash", "fedora",
     "Fedora @core group with man pages", FEDORA_CORE},
    {"arch", "Arch Linux", BACKEND_CONTAINER, PKG_PACMAN,
     "archlinux:latest", "linux/amd64", "/bin/bash", "archlinux",
     "Arch base group with man pages", ARCH_BASE},
    {"alpine", "Alpine Linux (latest)", BACKEND_CONTAINER, PKG_APK,
     "alpine:latest", NULL, "/bin/ash", "alpine",
     "BusyBox with man pages", ALPINE_BASE},
    {"macos", "macOS (this Mac)", BACKEND_NATIVE, PKG_BREW,
     NULL, NULL, "/bin/zsh", NULL,
     "macOS system tools only (/usr/bin /bin /usr/sbin /sbin)", NULL},
};

const os_def *os_list(size_t *n)
{
    *n = sizeof OSES / sizeof OSES[0];
    return OSES;
}

const os_def *os_find(const char *id)
{
    for (size_t i = 0; i < sizeof OSES / sizeof OSES[0]; i++)
        if (!strcmp(OSES[i].id, id))
            return &OSES[i];
    return NULL;
}

bool os_available(const os_def *os)
{
    if (os->backend == BACKEND_CONTAINER)
        return true;
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

const char *pkg_name(pkg_t pkg)
{
    switch (pkg) {
    case PKG_APT: return "apt";
    case PKG_DNF: return "dnf";
    case PKG_PACMAN: return "pacman";
    case PKG_APK: return "apk";
    case PKG_BREW: return "Homebrew";
    }
    return "?";
}
