#!/bin/sh
# Double-click in Finder to run TINY in Terminal (builds it the first time).
cd "$(dirname "$0")" || exit 1
[ -x ./tiny ] || ./build.sh || exit 1
exec ./tiny "$@"
