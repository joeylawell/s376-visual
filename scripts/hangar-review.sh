#!/usr/bin/env bash
# KJFK 04R review shot list + --bench for Joey's Mint (or this VM).
# Writes ~/Downloads/s376-review-<date>/ by default. Prefers a pbuffer
# (--offscreen) so projectors stay dark. Never commits imagery.
# Never talks to 10.1.95.0/24, 192.168.173.0/24, or UDP 3340/3341.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATE="$(date +%Y-%m-%d)"
OUT="${S376_REVIEW_DIR:-$HOME/Downloads/s376-review-$DATE}"
CACHE="${S376_CACHE:-}"
BIN=""
WAIT_SEC=3
SHOTS=1
BENCH=1
DRY=0
QUALITY="gtx980ti"

usage() {
    cat <<'EOF'
usage: scripts/hangar-review.sh [--cache DIR] [--out DIR] [--bin PATH] [--wait SEC] [--quality NAME] [--shots-only] [--bench-only] [--dry-run]

  --cache DIR     packed KJFK cache (default: $S376_CACHE, then
                  $ROOT/cache/KJFK, then ~/s376-visual/cache/KJFK)
  --out DIR       review directory (default: ~/Downloads/s376-review-<date>)
  --bin PATH      s376-visual binary (default: $ROOT/build-preview, then $ROOT/build)
  --wait SEC      --screenshot-wait (default 3)
  --quality NAME  gtx980ti | rtx | custom (default gtx980ti)
  --shots-only    skip --bench
  --bench-only    skip the shot list
  --dry-run       print commands and exit 0

Offscreen FBO / pbuffer: no chrome on the hangar projectors.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cache)
            CACHE="${2:?}"
            shift
            ;;
        --out)
            OUT="${2:?}"
            shift
            ;;
        --bin)
            BIN="${2:?}"
            shift
            ;;
        --wait)
            WAIT_SEC="${2:?}"
            shift
            ;;
        --quality)
            QUALITY="${2:?}"
            shift
            ;;
        --shots-only) BENCH=0 ;;
        --bench-only) SHOTS=0 ;;
        --dry-run) DRY=1 ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

looks_packed() {
    local d="$1"
    [[ -d "$d" ]] || return 1
    [[ -f "$d/elev.s376dem" || -f "$d/lod0/elev.s376dem" || -d "$d/lod0" ]]
}

if [[ -z "$CACHE" ]]; then
    for cand in "$ROOT/cache/KJFK" "$ROOT/cache/kjfk" "$HOME/s376-visual/cache/KJFK" \
                /tmp/kjfk-real; do
        if looks_packed "$cand"; then
            CACHE="$cand"
            break
        fi
    done
fi

if [[ -z "$BIN" ]]; then
    for cand in "$ROOT/build-preview/s376-visual" "$ROOT/build/s376-visual"; do
        if [[ -x "$cand" ]]; then
            BIN="$cand"
            break
        fi
    done
fi
if [[ -z "$BIN" ]]; then
    BIN="$ROOT/build/s376-visual"
fi

IG=("$BIN" --offscreen --quality "$QUALITY" --screenshot-wait "$WAIT_SEC")
if [[ -n "$CACHE" ]]; then
    IG+=(--cache-dir "$CACHE")
fi

run() {
    if [[ "$DRY" -eq 1 ]]; then
        printf '+'
        printf ' %q' "$@"
        printf '\n'
        return 0
    fi
    "$@"
}

if [[ "$DRY" -eq 0 ]]; then
    mkdir -p "$OUT"
fi

echo "hangar-review  cache=${CACHE:-NONE}  out=$OUT  bin=$BIN" >&2
if [[ "$DRY" -eq 0 && -z "$CACHE" ]]; then
    echo "no packed KJFK cache; set --cache or S376_CACHE" >&2
    exit 1
fi
if [[ "$DRY" -eq 0 && ! -x "$BIN" ]]; then
    echo "missing $BIN — build first (or pass --bin)" >&2
    exit 1
fi

NMS=(8 4 2.99 1 0.6 0.15 0)
TODS=(12 19 3)

if [[ "$SHOTS" -eq 1 ]]; then
    for nm in "${NMS[@]}"; do
        tag="$nm"
        [[ "$nm" == "0" ]] && tag="0-linedup"
        for tod in "${TODS[@]}"; do
            run "${IG[@]}" --at "KJFK:04R:$nm" --tod "$tod" --channel mid_left \
                --screenshot "$OUT/kjfk_04r_${tag}nm_tod${tod}_front.ppm"
            run "${IG[@]}" --at "KJFK:04R:$nm" --tod "$tod" --channel all \
                --screenshot "$OUT/kjfk_04r_${tag}nm_tod${tod}_wrap.ppm"
        done
    done
    run "${IG[@]}" --at KJFK --alt 12000 --hdg 036 --tod 12 --channel mid_left \
        --screenshot "$OUT/kjfk_12k_hdg036_tod12_front.ppm"
    run "${IG[@]}" --at KJFK --alt 12000 --hdg 036 --tod 12 --channel all \
        --screenshot "$OUT/kjfk_12k_hdg036_tod12_wrap.ppm"
    run "${IG[@]}" --at KJFK:04R:0.15 --tod 12 --rwy-cond 2 --channel mid_left \
        --screenshot "$OUT/kjfk_04r_0.15nm_tod12_wet_front.ppm"
    run "${IG[@]}" --at KJFK:13L:6 --tod 12 --channel mid_left \
        --screenshot "$OUT/kjfk_13l_6nm_tod12_front.ppm"
    run "${IG[@]}" --at KJFK:13L:6 --tod 19 --channel mid_left \
        --screenshot "$OUT/kjfk_13l_6nm_tod19_front.ppm"
    run "${IG[@]}" --at KJFK:13L:6 --tod 19 --wet --channel mid_left \
        --screenshot "$OUT/kjfk_13l_6nm_tod19_wet_front.ppm"
    # Manhattan skyline (NW of JFK) + short-final terminals, day and night.
    run "${IG[@]}" --at KJFK --alt 2500 --hdg 310 --tod 12 --channel mid_left \
        --screenshot "$OUT/kjfk_manhattan_hdg310_tod12_front.ppm"
    run "${IG[@]}" --at KJFK --alt 2500 --hdg 310 --tod 3 --channel mid_left \
        --screenshot "$OUT/kjfk_manhattan_hdg310_tod3_front.ppm"
    run "${IG[@]}" --at KJFK --alt 2500 --hdg 310 --tod 12 --channel all \
        --screenshot "$OUT/kjfk_manhattan_hdg310_tod12_wrap.ppm"
fi

if [[ "$BENCH" -eq 1 ]]; then
    if [[ "$DRY" -eq 1 ]]; then
        printf '+'
        printf ' %q' "${IG[@]}" --bench --approach KJFK:04R
        printf ' > %q\n' "$OUT/bench.txt"
    else
        "${IG[@]}" --bench --approach KJFK:04R | tee "$OUT/bench.txt"
    fi
fi

echo "hangar-review wrote $OUT" >&2
