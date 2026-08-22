#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /path/to/clean/libfprint-v1.94.100" >&2
    exit 64
fi

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
target=$1
expected=80a4b5ec612892c5c056c48dddaa561452cf37ec

if [ ! -d "$target/.git" ] || [ "$(git -C "$target" rev-parse HEAD)" != "$expected" ]; then
    echo "target must be a clean libfprint v1.94.100 checkout ($expected)" >&2
    exit 65
fi
if [ -n "$(git -C "$target" status --porcelain)" ]; then
    echo "target checkout has local changes; refusing to overwrite it" >&2
    exit 65
fi

install -d "$target/libfprint/drivers/goodix_gm168"
install -m 0644 "$repo_root"/driver/* "$target/libfprint/drivers/goodix_gm168/"
patch --directory="$target" --strip=1 --forward --batch \
    <"$repo_root/patches/libfprint-v1.94.100-integration.patch"

echo "GM168 overlay applied to $target"
