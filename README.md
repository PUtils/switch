# switch (`sw`)

Part of **PUtils**. `sw` switches your terminal into a separate shell profile
that looks and behaves like another system while you keep working on your own
files. The default profile, `debian13`, is Debian 13 (trixie) with its standard
tools and bash. Use it to practise in the same environment as the uni machines,
without your Mac's packages or dotfiles getting in the way.

```
$ sw                     # Debian 13, in the directory you're in
you@debian:~/Desktop$ exit
$ sw -l ~/uni/home       # that directory becomes your (only) home
$ sw profile add mac macos git   # a clean macOS shell with only git from Homebrew
$ sw inspect debian13 tool add gcc make gdb
```

Linux profiles (Debian, Ubuntu, Fedora, Arch, Alpine) run the real distro in a
throw-away Docker container, with `/Users` shared at the same paths and your own
uid. macOS profiles run natively with a `PATH` of just the system tools and the
Homebrew formulae you pick. Full details: `man sw`.

## Install

Requires a C compiler and, for Linux profiles, Docker Desktop.

```
make
make install            # into ~/.local (PREFIX=/usr/local to change)
make test
```

This installs `sw`, the `switch` alias, and the `sw(1)` manual.

## Commands

| | |
|---|---|
| `sw [-l DIR] [PROFILE] [-c CMD]` | switch (default profile if none given) |
| `exit` / `sw exit` | go back |
| `sw profile add NAME [OS] [TOOL...]` | create a profile (`-s` shell, `-H` hostname, `-l` home, `-a` arch, `-m` mount) |
| `sw profile rm NAME` | delete a profile (not `debian13` or the current default) |
| `sw profile list` | list profiles |
| `sw profile inspect NAME` / `ins` | show a profile |
| `sw profile default [NAME]` | show or set the default |
| `sw profile set NAME KEY [VALUE]` | change os, shell, hostname, home, arch, mounts |
| `sw profile build NAME [--pull]` | rebuild a Linux profile's image |
| `sw inspect NAME tool [add\|rm TOOL...]` | list, add or remove a profile's tools |
| `sw os` | systems a profile can be |
