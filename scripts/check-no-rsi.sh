#!/usr/bin/env bash
# CI guard: fail if a Raster / RSI / CAE database file is in the public tree.
# Does not open those files. Does not contact any LAN.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail=0
while IFS= read -r -d '' f; do
    base="$(basename "$f")"
    low="$(printf '%s' "$base" | tr '[:upper:]' '[:lower:]')"
    case "$low" in
        nxhostpacket.h|rasterxt*|*.nxt|*.nxb|*.nx3|*.nxs|*.nxo)
            echo "FORBIDDEN $f" >&2
            fail=1
            ;;
    esac
done < <(find "$ROOT" \
    \( -path "$ROOT/.git" -o -path "$ROOT/build" -o -path "$ROOT/cache" \
       -o -path "$ROOT/downloads" -o -path "$ROOT/logs" \) -prune -o \
    -type f -print0)

if [[ "$fail" -ne 0 ]]; then
    echo "Raster/RSI files must not enter this repo." >&2
    exit 1
fi
echo "repo-guard ok"
