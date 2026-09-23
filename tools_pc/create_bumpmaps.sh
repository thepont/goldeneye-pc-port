#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
make -C "$repo_root/tools/mktex" create_bumpmaps
cd "$repo_root"
exec "$repo_root/tools/mktex/build/create_bumpmaps" "$@"
