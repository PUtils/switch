#!/bin/sh
# Offline tests for sw: profile management without Docker.
# Linux profiles are checked for real by switching to one (see README).
set -u
SW="${SW:-$(dirname "$0")/../build/sw}"
SW_HOME=$(mktemp -d)
export SW_HOME
trap 'rm -rf "$SW_HOME"' EXIT
fails=0

ok()   { if "$@" >/dev/null 2>&1; then printf 'ok    %s\n' "$*"; else printf 'FAIL  %s\n' "$*"; fails=$((fails + 1)); fi; }
fail() { if "$@" >/dev/null 2>&1; then printf 'FAIL  (should fail) %s\n' "$*"; fails=$((fails + 1)); else printf 'ok    ! %s\n' "$*"; fi; }
has()  { out=$("$SW" "$@" 2>&1); }

ok   "$SW" --version
ok   "$SW" profile list
has profile list;                  ok   sh -c "echo '$out' | grep -q '^\* debian13'"
ok   "$SW" inspect debian13
has inspect debian13 tool;         ok   sh -c "echo '$out' | grep -q '^(base)'"
fail "$SW" profile rm debian13 -f
fail "$SW" profile add BadName debian13
fail "$SW" profile add inspect debian13
fail "$SW" profile add x nosuchos
ok   "$SW" profile add uni debian13 gcc make -H lab01 -l '$HOME/uni'
fail "$SW" profile add uni debian13
has inspect uni tool;              ok   sh -c "echo '$out' | grep -qx gcc"
ok   grep -qx 'home=$HOME/uni' "$SW_HOME/profiles/uni/profile"
ok   "$SW" profile set uni hostname lab02
ok   grep -qx 'hostname=lab02' "$SW_HOME/profiles/uni/profile"
ok   "$SW" profile default uni
fail "$SW" profile rm uni -f
ok   "$SW" profile default debian13
ok   "$SW" profile rm uni -f
fail "$SW" inspect uni
if [ "$(uname)" = Darwin ]; then
    ok   "$SW" profile add mac macos
    has mac -c 'echo $PATH';       ok   sh -c "echo '$out' | grep -q '/usr/bin:/bin:/usr/sbin:/sbin$'"
    has mac -c 'command -v brew';  fail sh -c "echo '$out' | grep -q brew"
    has -l '$SW_HOME' mac -c 'echo $HOME'; ok sh -c "echo '$out' | grep -q '$(basename "$SW_HOME")\$'"
    fail "$SW" inspect mac tool add sw-no-such-formula
    has mac -c 'sw exit; echo still-here'; fail sh -c "echo '$out' | grep -q still-here"
fi
fail "$SW" exit

[ "$fails" -eq 0 ] && echo "all tests passed" || { echo "$fails failed"; exit 1; }
