#!/usr/bin/env bash
set -euo pipefail

PATCHES_DIR="$1"

sort_by_basename () {
    python3 -c '
import os, sys
paths=sys.stdin.read().split("\0")[:-1]
paths.sort(key=os.path.basename)
sys.stdout.write("\0".join(paths))
'
}

readarray -d '' PATCH_FILES < <(find -L "$PATCHES_DIR" -type f -print0 | sort_by_basename)

for patch in "${PATCH_FILES[@]}"; do
    if patch -R -p1 -s -f --dry-run 2>/dev/null < "$patch"; then
        echo "Patch file already applied, skipping: ${patch@Q}"
        continue
    fi
    patch -p1 --forward < "$patch"
done
