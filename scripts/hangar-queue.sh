#!/usr/bin/env bash
# Resumable unattended hangar job queue for Joey's Mint 22.3 (RTX 5060 Ti,
# 16 GB RAM, repo ~/s376-visual, cache ~/s376-visual/cache/KJFK).
#
# Each step writes ~/Downloads/s376-queue/<NN>-<name>/ plus a `done`
# marker. Rerun skips finished steps. STATUS.txt + SUMMARY.md stay current.
#
# Never touches network config. Never sends to UDP 3340/3341. Never
# contacts 10.1.95.0/24 or 192.168.173.0/24. No sudo. Offscreen FBO /
# pbuffer only — no projector windows. If a game is running, wait and
# retry every 5 minutes; never kill anything.
#
# Crash / reboot resilience (Joey's 16 GB Mint froze once):
#   user systemd autostart (--install-autostart, no sudo) resumes after reboot;
#   heartbeat + boot_id detect an unclean stop; journal OOM/Xid/thermal;
#   per-step crash counters step down (-j 16→8→4, rtx→gtx980ti, serial
#   fetches, lower heat limit) and BLOCKED after 3; PSI/MemAvailable
#   watchdog pauses or stops only this queue's units (~4 GB free);
#   CRASHES.md. Renderer/cache take --mem-budget so one process cannot
#   eat the box. Layout stays ~/Downloads/s376-queue/<NN>-<name>/.
#
# usage: scripts/hangar-queue.sh [--from NN] [--only NN] [--list] [--dry-run] [--root DIR] [--out DIR] [--kjfk DIR] [--all-airports]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${S376_QUEUE_DIR:-$HOME/Downloads/s376-queue}"
KJFK="${S376_CACHE:-$ROOT/cache/KJFK}"
FROM=""
ONLY=""
DRY=0
LIST=0
FORCE=0
ALL_AIRPORTS=0
# Mint redline: 16 fetch/build workers, one serial step at 12G, ~4G left
# for the desktop (16 GB box). Override with -j / $S376_JOBS.
JOBS="${S376_JOBS:-16}"
WAIT_SEC="${S376_GPU_WAIT_SEC:-300}"
MEM_BUDGET_MB="${S376_MEM_BUDGET:-2048}"
KEEP_FREE_MB="${S376_KEEP_FREE_MB:-4096}"
HEAT_LIMIT="${S376_HEAT_LIMIT:-83}"
MAX_CRASHES="${S376_MAX_CRASHES:-3}"
QUALITY_CAP="${S376_QUALITY_CAP:-rtx}"
SERIAL_FETCH=0
INSTALL_AUTOSTART=0
UNINSTALL_AUTOSTART=0
CRASH_CHECK=0
CURRENT_NN=""
CURRENT_NAME=""
WATCH_PIDS=()

# NN name mem. Default is KJFK-only (Joey demo). KEWR / other US fields
# stay behind --all-airports so the hangar spends the night on JFK.
STEPS=(
    "01 build 12G"
    "02 review-now 12G"
    "03 bench 12G"
    "04 video-day 12G"
    "05 video-night 12G"
    "06 fetch-kjfk-far 12G"
    "07 fetch-kjfk-near 12G"
    "08 review-after-fetch 12G"
    "09 demo-kjfk-day 12G"
    "10 demo-kjfk-night 12G"
    "11 bench-repeat 12G"
    "12 skyline-water 12G"
)

usage() {
    cat <<'EOF'
usage: scripts/hangar-queue.sh [--from NN] [--only NN] [--list] [--dry-run] [--root DIR] [--out DIR] [--kjfk DIR] [--all-airports] [-j N]

  --from NN         start at this step (still skip any later step that has `done`)
  --only NN         run only this step (skip if `done` unless --force)
  --list            print the step table and exit 0
  --dry-run         print commands, write nothing, exit 0
  --force           ignore `done` markers for selected steps
  --root DIR        repo (default: parent of this script, Joey: ~/s376-visual)
  --out DIR         queue dir (default: ~/Downloads/s376-queue)
  --kjfk DIR        existing 1 m KJFK pack (default: $ROOT/cache/KJFK)
  --all-airports    also run 13 fetch-kewr and 14 fetch-us-next (paused by default)
  -j, --jobs N      make / fetch workers (default 16; $S376_JOBS)
  --mem-budget MB   pass --mem-budget to s376-visual / s376-cache (default 2048)
  --install-autostart   user systemd unit that resumes after reboot (no sudo)
  --uninstall-autostart remove that user unit
  --crash-check     print heartbeat / boot_id / CRASHES.md and exit 0

Default steps (KJFK demo): 01 build, 02 review-now, 03 bench, 04 video-day,
05 video-night, 06 fetch-kjfk-far, 07 fetch-kjfk-near, 08 review-after-fetch,
09 demo-kjfk-day, 10 demo-kjfk-night, 11 bench-repeat, 12 skyline-water.

Heavy work is memory-capped with:
  systemd-run --user --scope -p MemoryMax=… -p MemorySwapMax=0
GPU steps wait if Steam / X-Plane / another fullscreen GL app is up.
Fetch steps refuse to start when free disk is too low (300 GB floor
for the opt-in US-next loop). ffmpeg is optional — PNG frames are kept.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --from)
            FROM="${2:?}"
            shift
            ;;
        --only)
            ONLY="${2:?}"
            shift
            ;;
        --list) LIST=1 ;;
        --dry-run) DRY=1 ;;
        --force) FORCE=1 ;;
        --root)
            ROOT="${2:?}"
            shift
            ;;
        --out)
            OUT="${2:?}"
            shift
            ;;
        --kjfk)
            KJFK="${2:?}"
            shift
            ;;
        --all-airports) ALL_AIRPORTS=1 ;;
        -j|--jobs)
            JOBS="${2:?}"
            shift
            ;;
        --mem-budget)
            MEM_BUDGET_MB="${2:?}"
            shift
            ;;
        --install-autostart) INSTALL_AUTOSTART=1 ;;
        --uninstall-autostart) UNINSTALL_AUTOSTART=1 ;;
        --crash-check) CRASH_CHECK=1 ;;
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

step_nn() { echo "$1" | awk '{print $1}'; }
step_name() { echo "$1" | awk '{print $2}'; }
step_mem() { echo "$1" | awk '{print $3}'; }

step_dir() {
    local nn="$1" name="$2"
    echo "$OUT/${nn}-${name}"
}

normalize_nn() {
    local n="$1"
    n="${n#0}"
    [[ -z "$n" ]] && n=0
    printf '%02d' "$n"
}

if [[ "$ALL_AIRPORTS" -eq 1 ]]; then
    STEPS+=(
        "13 fetch-kewr 12G"
        "14 fetch-us-next 12G"
    )
fi

if [[ "$LIST" -eq 1 ]]; then
    echo "s376 hangar-queue"
    echo "out=$OUT"
    echo "root=$ROOT"
    echo "kjfk=$KJFK"
    for s in "${STEPS[@]}"; do
        nn="$(step_nn "$s")"
        name="$(step_name "$s")"
        d="$(step_dir "$nn" "$name")"
        mark="pending"
        [[ -f "$d/done" ]] && mark="done"
        [[ -f "$d/BLOCKED" ]] && mark="BLOCKED"
        printf '%s  %-20s  mem=%-4s  %s\n' "$nn" "$name" "$(step_mem "$s")" "$mark"
    done
    exit 0
fi

boot_id() {
    if [[ -r /proc/sys/kernel/random/boot_id ]]; then
        tr -d '[:space:]' </proc/sys/kernel/random/boot_id
    else
        echo unknown
    fi
}

mem_available_mb() {
    awk '/MemAvailable:/ {print int($2/1024); exit}' /proc/meminfo 2>/dev/null || echo 0
}

psi_some_avg10() {
    awk '/^some / {for(i=1;i<=NF;i++) if($i ~ /^avg10=/) {split($i,a,"="); print a[2]; exit}}' \
        /proc/pressure/memory 2>/dev/null || echo 0
}

gpu_temp_c() {
    if command -v nvidia-smi >/dev/null 2>&1; then
        nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>/dev/null | head -n 1 | tr -dc '0-9'
    fi
}

write_unit() {
    local unit_dir="$HOME/.config/systemd/user"
    local unit="$unit_dir/s376-hangar-queue.service"
    if [[ "$DRY" -eq 1 ]]; then
        echo "+ write $unit"
        echo "+ ExecStart=$ROOT/scripts/hangar-queue.sh --out $OUT --root $ROOT --kjfk $KJFK"
        echo "+ systemctl --user enable --now s376-hangar-queue.service"
        return 0
    fi
    mkdir -p "$unit_dir"
    cat >"$unit" <<EOF
[Unit]
Description=s376 hangar-queue resume after reboot
After=default.target

[Service]
Type=oneshot
WorkingDirectory=$ROOT
ExecStart=$ROOT/scripts/hangar-queue.sh --out $OUT --root $ROOT --kjfk $KJFK
RemainAfterExit=no

[Install]
WantedBy=default.target
EOF
    if command -v systemctl >/dev/null 2>&1; then
        systemctl --user daemon-reload
        systemctl --user enable s376-hangar-queue.service
        echo "enabled user unit $unit (no sudo). reboot resumes the queue."
    else
        echo "wrote $unit (systemctl not on PATH — enable later)"
    fi
}

remove_unit() {
    local unit="$HOME/.config/systemd/user/s376-hangar-queue.service"
    if [[ "$DRY" -eq 1 ]]; then
        echo "+ systemctl --user disable --now s376-hangar-queue.service"
        echo "+ rm -f $unit"
        return 0
    fi
    if command -v systemctl >/dev/null 2>&1; then
        systemctl --user disable --now s376-hangar-queue.service >/dev/null 2>&1 || true
    fi
    rm -f "$unit"
    echo "removed user hangar-queue autostart"
}

if [[ "$INSTALL_AUTOSTART" -eq 1 ]]; then
    write_unit
    exit 0
fi
if [[ "$UNINSTALL_AUTOSTART" -eq 1 ]]; then
    remove_unit
    exit 0
fi
if [[ "$CRASH_CHECK" -eq 1 ]]; then
    echo "s376 hangar-queue crash-check"
    echo "out=$OUT"
    echo "boot_id=$(boot_id)"
    echo "mem_available_mb=$(mem_available_mb)"
    echo "psi_some_avg10=$(psi_some_avg10)"
    echo "heat_limit=$HEAT_LIMIT"
    if [[ -f "$OUT/heartbeat" ]]; then
        echo "heartbeat: $(cat "$OUT/heartbeat")"
    else
        echo "heartbeat: none"
    fi
    if [[ -f "$OUT/boot_id" ]]; then
        echo "last_boot: $(cat "$OUT/boot_id")"
    fi
    if [[ -f "$OUT/CRASHES.md" ]]; then
        echo "--- CRASHES.md ---"
        cat "$OUT/CRASHES.md"
    else
        echo "CRASHES.md: none"
    fi
    exit 0
fi

# Refuse operator mistakes that would talk to the sim / visual LAN.
for a in "$ROOT" "$OUT" "$KJFK" "${FROM:-}" "${ONLY:-}"; do
    case "$a" in
        *10.1.95.*|*192.168.173.*|*:3340*|:3341*|*3340*|*3341*)
            echo "refusing sim/visual LAN or UDP 3340/3341 path: $a" >&2
            exit 2
            ;;
    esac
done

HAVE_SCOPE=0
if [[ "$DRY" -eq 0 ]] && command -v systemd-run >/dev/null 2>&1; then
    if systemd-run --user --scope --quiet --collect true >/dev/null 2>&1; then
        HAVE_SCOPE=1
    fi
fi

if [[ "$DRY" -eq 0 ]]; then
    mkdir -p "$OUT"
    exec 9>"$OUT/queue.lock"
    if ! flock -n 9; then
        echo "another hangar-queue holds $OUT/queue.lock" >&2
        exit 1
    fi
fi

PREVIEW="$ROOT/build-preview"
VISUAL="$PREVIEW/s376-visual"
CACHE_BIN="$PREVIEW/s376-cache"
DOCTOR="$PREVIEW/s376-doctor"
TESTS="$PREVIEW/s376-tests"
REVIEW="$ROOT/scripts/hangar-review.sh"
STATUS="$OUT/STATUS.txt"
SUMMARY="$OUT/SUMMARY.md"
CFG="$ROOT/config/s376-visual.toml"

disk_avail_gb() {
    local p="$1"
    mkdir -p "$p" 2>/dev/null || true
    df -BG --output=avail "$p" 2>/dev/null | tail -n 1 | tr -dc '0-9' || true
}

count_tiles() {
    find "$1" -name elev.s376dem 2>/dev/null | wc -l | tr -d ' ' || true
}

dir_size() {
    du -sh "$1" 2>/dev/null | awk '{print $1}' || true
}

log() {
    local line
    line="$(printf '%s %s' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*")"
    echo "$line" >&2
    if [[ "$DRY" -eq 0 ]]; then
        mkdir -p "$OUT"
        echo "$line" >>"$OUT/queue.log"
    fi
}

write_status() {
    local tmp="$STATUS.tmp"
    {
        echo "s376 hangar-queue"
        echo "updated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "root: $ROOT"
        echo "kjfk: $KJFK"
        echo "out: $OUT"
        echo "disk_free_gb: $(disk_avail_gb "$OUT")"
        echo
        if [[ -f "$OUT/current" ]]; then
            echo "current: $(cat "$OUT/current")"
        else
            echo "current: idle"
        fi
        echo
        printf '%-4s %-20s %-6s %s\n' "NN" "NAME" "STATE" "DETAIL"
        for s in "${STEPS[@]}"; do
            local nn name d state detail
            nn="$(step_nn "$s")"
            name="$(step_name "$s")"
            d="$(step_dir "$nn" "$name")"
            state="pending"
            detail=""
            if [[ -f "$d/BLOCKED" ]]; then
                state="BLOCK"
                detail="$(tr '\n' ' ' <"$d/BLOCKED" | head -c 160)"
            elif [[ -f "$d/done" ]]; then
                state="PASS"
                detail="$(tr '\n' ' ' <"$d/done" | head -c 160)"
            elif [[ -f "$d/fail" ]]; then
                state="FAIL"
                detail="$(tr '\n' ' ' <"$d/fail" | head -c 160)"
            elif [[ -f "$OUT/current" ]] && grep -q "^${nn} " "$OUT/current"; then
                state="RUN"
                detail="$(cat "$OUT/current")"
            fi
            printf '%-4s %-20s %-6s %s\n' "$nn" "$name" "$state" "$detail"
        done
    } >"$tmp"
    mv "$tmp" "$STATUS"
}

init_summary() {
    if [[ ! -f "$SUMMARY" ]]; then
        cat >"$SUMMARY" <<EOF
# s376 hangar queue

Started: $(date -u +%Y-%m-%dT%H:%M:%SZ)
Repo: \`$ROOT\`
KJFK cache: \`$KJFK\`
Output: \`$OUT\`

Grab frames from the **best frames** lists below. Imagery stays on this
PC — never commit it.

EOF
    fi
}

append_summary() {
    local title="$1"
    shift
    [[ "$DRY" -eq 1 ]] && return 0
    {
        echo "## $title"
        echo
        for line in "$@"; do
            echo "- $line"
        done
        echo
    } >>"$SUMMARY"
}

capped() {
    local mem="$1"
    shift
    wait_pressure
    if [[ "$DRY" -eq 1 ]]; then
        printf '+ systemd-run --user --scope --unit=s376-q-%s -p MemoryMax=%s -p MemorySwapMax=0 --' \
            "${CURRENT_NN:-xx}" "$mem"
        printf ' %q' "$@"
        printf '\n'
        return 0
    fi
    if [[ "$HAVE_SCOPE" -eq 1 ]]; then
        local unit="s376-q-${CURRENT_NN:-xx}-$$-$RANDOM"
        echo "$unit" >>"$OUT/our-units"
        systemd-run --user --scope --unit="$unit" --quiet --collect \
            -p "MemoryMax=$mem" -p MemorySwapMax=0 -- "$@"
    else
        "$@"
    fi
}

load_limits() {
    [[ -f "$OUT/limits" ]] || return 0
    # shellcheck disable=SC1090,SC1091
    source "$OUT/limits"
}

save_limits() {
    [[ "$DRY" -eq 1 ]] && return 0
    {
        echo "JOBS=$JOBS"
        echo "QUALITY_CAP=$QUALITY_CAP"
        echo "SERIAL_FETCH=$SERIAL_FETCH"
        echo "HEAT_LIMIT=$HEAT_LIMIT"
        echo "MEM_BUDGET_MB=$MEM_BUDGET_MB"
    } >"$OUT/limits"
}

note_crash() {
    local nn="$1" name="$2" why="$3"
    [[ "$DRY" -eq 1 ]] && return 0
    mkdir -p "$OUT"
    {
        echo "## $(date -u +%Y-%m-%dT%H:%M:%SZ)  $nn $name"
        echo
        echo "- $why"
        echo "- jobs=$JOBS quality_cap=$QUALITY_CAP serial_fetch=$SERIAL_FETCH heat=$HEAT_LIMIT"
        echo "- mem_available_mb=$(mem_available_mb) psi=$(psi_some_avg10) boot=$(boot_id)"
        echo
    } >>"$OUT/CRASHES.md"
}

step_down() {
    local n="$1"
    if [[ "$n" -ge 2 ]]; then
        JOBS=4
        QUALITY_CAP=gtx980ti
        SERIAL_FETCH=1
        HEAT_LIMIT=75
    elif [[ "$n" -ge 1 ]]; then
        if [[ "$JOBS" -gt 8 ]]; then
            JOBS=8
        fi
        HEAT_LIMIT=80
    fi
    save_limits
    log "step-down crashes=$n jobs=$JOBS quality_cap=$QUALITY_CAP serial=$SERIAL_FETCH heat=$HEAT_LIMIT"
}

stop_our_units() {
    local u
    [[ -f "$OUT/our-units" ]] || return 0
    while read -r u; do
        [[ -z "$u" ]] && continue
        systemctl --user stop "$u" >/dev/null 2>&1 || true
    done <"$OUT/our-units"
}

wait_pressure() {
    local avail psi
    if [[ "$DRY" -eq 1 ]]; then
        return 0
    fi
    while true; do
        avail="$(mem_available_mb)"
        psi="$(psi_some_avg10)"
        avail="${avail:-0}"
        psi="${psi:-0}"
        if [[ "$avail" -gt 0 && "$avail" -lt 2048 ]]; then
            log "mem watchdog: MemAvailable=${avail} MiB — stopping queue units (keep desktop)"
            stop_our_units
            echo "pressure MemAvailable=${avail}" >"$OUT/current"
            write_status
            sleep 15
            continue
        fi
        if [[ "$avail" -gt 0 && "$avail" -lt "$KEEP_FREE_MB" ]]; then
            log "mem watchdog: MemAvailable=${avail} MiB < ${KEEP_FREE_MB} — pausing"
            echo "pressure wait MemAvailable=${avail}" >"$OUT/current"
            write_status
            sleep 10
            continue
        fi
        # bash cannot do float compare portably; strip the decimal.
        local psi_i
        psi_i="${psi%%.*}"
        psi_i="${psi_i:-0}"
        if [[ "$psi_i" -ge 20 ]]; then
            log "mem watchdog: PSI some avg10=$psi — pausing"
            sleep 10
            continue
        fi
        break
    done
    local t
    t="$(gpu_temp_c)"
    if [[ -n "$t" && "$t" -ge "$HEAT_LIMIT" ]]; then
        log "thermal: GPU ${t}C >= ${HEAT_LIMIT} — waiting"
        sleep 20
    fi
}

journal_scan() {
    [[ "$DRY" -eq 1 ]] && return 0
    command -v journalctl >/dev/null 2>&1 || return 0
    local hits
    hits="$(journalctl --user -b --no-pager -n 120 2>/dev/null | \
        grep -iE 'oom|out of memory|killed process|xid|nvrm|thermal' || true)"
    if [[ -n "$hits" ]]; then
        note_crash "00" "journal" "boot journal has OOM/Xid/thermal"
        echo "$hits" >>"$OUT/CRASHES.md"
    fi
}

# Heartbeat / leftover `current` → increment that step's crash counter,
# step down, and BLOCKED after MAX_CRASHES. Used after a freeze/reboot.
note_unclean_step() {
    local why="$1" nn="" name="" d crashes
    if [[ -f "$OUT/current" ]]; then
        nn="$(awk '{print $1}' "$OUT/current")"
        name="$(awk '{print $2}' "$OUT/current")"
    elif [[ -f "$OUT/heartbeat" ]]; then
        nn="$(awk '{print $4}' "$OUT/heartbeat")"
        name="$(awk '{print $5}' "$OUT/heartbeat")"
    fi
    if [[ -z "$nn" || -z "$name" || "$nn" == "00" ]]; then
        note_crash "00" "unclean" "$why"
        return 0
    fi
    d="$(step_dir "$nn" "$name")"
    mkdir -p "$d"
    crashes=0
    if [[ -f "$d/crashes" ]]; then
        crashes="$(tr -dc '0-9' <"$d/crashes")"
        crashes="${crashes:-0}"
    fi
    crashes=$((crashes + 1))
    echo "$crashes" >"$d/crashes"
    note_crash "$nn" "$name" "$why (crash $crashes/$MAX_CRASHES)"
    step_down "$crashes"
    if [[ "$crashes" -ge "$MAX_CRASHES" ]]; then
        echo "BLOCKED after $crashes crashes $(date -u +%Y-%m-%dT%H:%M:%SZ)" >"$d/BLOCKED"
        log "BLOCKED $nn $name after unclean stop"
    fi
}

detect_unclean() {
    [[ "$DRY" -eq 1 ]] && return 0
    local now last unclean=0 why=""
    now="$(boot_id)"
    if [[ -f "$OUT/heartbeat" ]]; then
        last="$(awk '{print $2}' "$OUT/heartbeat" 2>/dev/null || true)"
        if [[ -n "$last" && "$last" != "$now" && "$last" != "unknown" ]]; then
            unclean=1
            why="unclean stop: heartbeat boot_id=$last now=$now"
        fi
    fi
    if [[ -f "$OUT/current" ]]; then
        unclean=1
        if [[ -z "$why" ]]; then
            why="unclean stop: leftover current=$(tr '\n' ' ' <"$OUT/current")"
        fi
    fi
    echo "$now" >"$OUT/boot_id"
    if [[ "$unclean" -eq 1 ]]; then
        log "$why — resuming leftover steps"
        note_unclean_step "$why"
    fi
}

heartbeat_loop() {
    while [[ -f "$OUT/queue.lock" ]]; do
        echo "$(date +%s) $(boot_id) $$ ${CURRENT_NN:-} ${CURRENT_NAME:-}" >"$OUT/heartbeat"
        sleep 15
    done
}

start_watchers() {
    [[ "$DRY" -eq 1 ]] && return 0
    heartbeat_loop &
    WATCH_PIDS+=($!)
}

stop_watchers() {
    local p
    for p in "${WATCH_PIDS[@]:-}"; do
        kill "$p" >/dev/null 2>&1 || true
    done
    WATCH_PIDS=()
}

# Steam / Proton (`reaper`, steamapps), X-Plane, or a fullscreen GL window
# that is not s376-visual. Never kill. Retry every WAIT_SEC.
gpu_busy() {
    if pgrep -af -- 'steamapps' 2>/dev/null | grep -Ev 's376-visual|hangar-queue|hangar-review|grep' | grep -q .; then
        return 0
    fi
    if pgrep -x reaper >/dev/null 2>&1; then
        return 0
    fi
    if pgrep -if -- 'x-?plane|xplane[[:space:]]' >/dev/null 2>&1; then
        return 0
    fi
    if [[ -n "${DISPLAY:-}" ]] && command -v xprop >/dev/null 2>&1; then
        local id state cls
        for id in $(xprop -root _NET_CLIENT_LIST 2>/dev/null | grep -o '0x[0-9a-fA-F]*'); do
            state="$(xprop -id "$id" _NET_WM_STATE 2>/dev/null || true)"
            echo "$state" | grep -q _NET_WM_STATE_FULLSCREEN || continue
            cls="$(xprop -id "$id" WM_CLASS 2>/dev/null || true)"
            echo "$cls" | grep -qi 's376' && continue
            return 0
        done
    fi
    return 1
}

wait_gpu() {
    local reason
    if [[ "$DRY" -eq 1 ]]; then
        echo "+ wait_gpu (skip if steamapps/reaper/x-plane/fullscreen GL)"
        return 0
    fi
    while gpu_busy; do
        reason="game or fullscreen GL running — waiting ${WAIT_SEC}s (never killing)"
        echo "waiting: $reason" >"$OUT/current"
        write_status
        log "$reason"
        sleep "$WAIT_SEC"
    done
}

need_disk() {
    local min_gb="$1" dest="$2"
    local have
    have="$(disk_avail_gb "$dest")"
    have="${have:-0}"
    if [[ "$have" -lt "$min_gb" ]]; then
        echo "need ${min_gb} GB free at $dest (have ${have} GB)" >&2
        return 1
    fi
    return 0
}

ig() {
    local mem="$1"
    shift
    wait_gpu
    local extra=(--mem-budget "$MEM_BUDGET_MB")
    capped "$mem" "$VISUAL" --offscreen -c "$CFG" "${extra[@]}" "$@"
}

shot() {
    local mem="$1" dest="$2"
    shift 2
    [[ "$DRY" -eq 0 ]] && mkdir -p "$(dirname "$dest")"
    ig "$mem" --screenshot-wait 3 --screenshot "$dest" "$@"
}

doctor_cache() {
    local mem="$1" cache="$2" logf="$3"
    if [[ "$DRY" -eq 1 ]]; then
        echo "+ doctor --cache-dir $cache --no-bench"
        return 0
    fi
    # Doctor fails are recorded; they do not fail a completed fetch (resume).
    if ! capped "$mem" "$DOCTOR" -c "$CFG" --cache-dir "$cache" --no-bench | tee "$logf"; then
        echo "doctor reported fails (see log)" | tee "$logf.fail"
    fi
    return 0
}

fetch_pack() {
    local mem="$1" icao="$2" dest="$3" img="$4"
    shift 4
    if [[ "$DRY" -eq 0 ]]; then
        need_disk 20 "$(dirname "$dest")" || return 1
    fi
    local fj="$JOBS"
    if [[ "$SERIAL_FETCH" -eq 1 ]]; then
        fj=1
    fi
    capped "$mem" "$CACHE_BIN" fetch "$icao" --out "$dest" --dem 3dep \
        --imagery "$img" --max-export 2048 --jobs "$fj" --mem-budget "$MEM_BUDGET_MB" "$@"
    capped 4G "$CACHE_BIN" osm "$icao" --out "$dest" --radius-km 12 || true
}

contact_sheet() {
    local before="$1" after="$2" dest="$3"
    if [[ "$DRY" -eq 1 ]]; then
        echo "+ python3 contact-sheet $before $after -> $dest"
        return 0
    fi
    if ! python3 -c 'from PIL import Image' >/dev/null 2>&1; then
        echo "PIL missing; skip contact sheet (no ImageMagick)" >"$dest.skipped"
        return 0
    fi
    python3 - "$before" "$after" "$dest" <<'PY'
import os, sys
from pathlib import Path
from PIL import Image

before, after, dest = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])

def pairs():
    names = []
    for p in sorted(before.glob("*.png")) + sorted(before.glob("*.ppm")):
        other = after / p.name
        if other.exists():
            names.append((p, other))
    # unique stems
    seen = set()
    out = []
    for a, b in names:
        if a.stem in seen:
            continue
        seen.add(a.stem)
        out.append((a, b))
    return out

def load(p):
    im = Image.open(p).convert("RGB")
    im.thumbnail((960, 600))
    return im

rows = pairs()
if not rows:
    Path(str(dest) + ".skipped").write_text("no matching png/ppm pairs\n")
    sys.exit(0)
gap = 8
label_h = 22
cell_w, cell_h = 960, 600
W = cell_w * 2 + gap * 3
H = (cell_h + label_h + gap) * len(rows) + gap
sheet = Image.new("RGB", (W, H), (18, 20, 24))
y = gap
for a, b in rows:
    la, lb = load(a), load(b)
    sheet.paste(la, (gap, y + label_h))
    sheet.paste(lb, (gap * 2 + cell_w, y + label_h))
    y += cell_h + label_h + gap
dest.parent.mkdir(parents=True, exist_ok=True)
sheet.save(dest)
print(f"wrote {dest}  {len(rows)} pairs")
PY
}

should_run() {
    local nn="$1"
    if [[ -n "$ONLY" && "$(normalize_nn "$ONLY")" != "$nn" ]]; then
        return 1
    fi
    if [[ -n "$FROM" && "$nn" < "$(normalize_nn "$FROM")" ]]; then
        return 1
    fi
    return 0
}

run_step() {
    local nn="$1" name="$2" mem="$3"
    local d
    d="$(step_dir "$nn" "$name")"
    if [[ "$FORCE" -eq 0 && -f "$d/done" ]]; then
        log "skip $nn $name (done)"
        return 0
    fi
    if [[ "$FORCE" -eq 0 && -f "$d/BLOCKED" ]]; then
        log "skip $nn $name (BLOCKED after ${MAX_CRASHES} crashes)"
        return 0
    fi
    if ! should_run "$nn"; then
        return 0
    fi
    CURRENT_NN="$nn"
    CURRENT_NAME="$name"
    if [[ "$DRY" -eq 1 ]]; then
        log "DRY $nn $name"
        set +e
        "step_${name//-/_}" "$d" "$mem"
        set -e
        CURRENT_NN=""
        CURRENT_NAME=""
        return 0
    fi
    mkdir -p "$d"
    echo "$nn $name starting $(date -u +%Y-%m-%dT%H:%M:%SZ)" >"$OUT/current"
    write_status
    log "START $nn $name"
    local t0 t1 elapsed rc crashes
    t0="$(date +%s)"
    rc=0
    set +e
    "step_${name//-/_}" "$d" "$mem"
    rc=$?
    set -e
    t1="$(date +%s)"
    elapsed="$((t1 - t0))"
    if [[ "$rc" -eq 0 ]]; then
        echo "PASS ${elapsed}s $(date -u +%Y-%m-%dT%H:%M:%SZ)" >"$d/done"
        rm -f "$d/fail"
        log "PASS $nn $name ${elapsed}s"
        write_status
        CURRENT_NN=""
        CURRENT_NAME=""
        return 0
    fi
    echo "FAIL rc=$rc ${elapsed}s $(date -u +%Y-%m-%dT%H:%M:%SZ)" >"$d/fail"
    crashes=0
    if [[ -f "$d/crashes" ]]; then
        crashes="$(tr -dc '0-9' <"$d/crashes")"
        crashes="${crashes:-0}"
    fi
    crashes=$((crashes + 1))
    echo "$crashes" >"$d/crashes"
    note_crash "$nn" "$name" "step failed rc=$rc after ${elapsed}s (crash $crashes/$MAX_CRASHES)"
    step_down "$crashes"
    log "FAIL $nn $name rc=$rc ${elapsed}s crashes=$crashes"
    write_status
    if [[ "$crashes" -ge "$MAX_CRASHES" ]]; then
        echo "BLOCKED after $crashes crashes $(date -u +%Y-%m-%dT%H:%M:%SZ)" >"$d/BLOCKED"
        log "BLOCKED $nn $name"
        write_status
    fi
    CURRENT_NN=""
    CURRENT_NAME=""
    if [[ "$nn" == "01" ]]; then
        echo "build failed — stopping queue" >&2
        return "$rc"
    fi
    return 0
}

# --- steps ---

step_build() {
    local d="$1" mem="$2"
    if [[ "$DRY" -eq 0 ]]; then
        git -C "$ROOT" pull --ff-only >"$d/git-pull.log" 2>&1 || \
            echo "git pull --ff-only failed (building existing tree)" | tee -a "$d/git-pull.log"
    else
        echo "+ git -C $ROOT pull --ff-only"
    fi
    capped "$mem" cmake -S "$ROOT" -B "$PREVIEW" -DCMAKE_BUILD_TYPE=Release
    capped "$mem" cmake --build "$PREVIEW" -j "$JOBS"
    if [[ "$DRY" -eq 1 ]]; then
        echo "+ $TESTS"
        append_summary "01 build" "result: DRY-RUN" "bin: \`$PREVIEW\`"
        return 0
    fi
    set +e
    capped 4G "$TESTS" | tee "$d/tests.log"
    local rc=${PIPESTATUS[0]}
    set -e
    local counts
    counts="$(grep -E '[0-9]+ passed, [0-9]+ failed' "$d/tests.log" | tail -n 1 || true)"
    append_summary "01 build" \
        "result: $([[ $rc -eq 0 ]] && echo PASS || echo FAIL)" \
        "tests: ${counts:-unknown}" \
        "binary: \`$VISUAL\`" \
        "log: \`$d/tests.log\`"
    return "$rc"
}

step_review_now() {
    local d="$1" mem="$2"
    wait_gpu
    if [[ ! -x "$REVIEW" ]]; then
        echo "missing $REVIEW" >&2
        return 1
    fi
    capped "$mem" "$REVIEW" --cache "$KJFK" --out "$d/shots" --bin "$VISUAL" \
        --quality gtx980ti --shots-only --wait 3
    local n
    n="$(find "$d/shots" \( -name '*.png' -o -name '*.ppm' \) 2>/dev/null | wc -l | tr -d ' ' || true)"
    append_summary "02 review-now" \
        "result: PASS" \
        "shots: ${n} files in \`$d/shots\`" \
        "best frames: \`$d/shots/kjfk_04r_2.99nm_tod12_front.png\`" \
        "\`$d/shots/kjfk_04r_0.6nm_tod12_front.png\`" \
        "\`$d/shots/kjfk_04r_0.15nm_tod12_front.png\`" \
        "\`$d/shots/kjfk_04r_2.99nm_tod12_wrap.png\`" \
        "\`$d/shots/kjfk_04r_0.6nm_tod3_front.png\`"
}

step_bench() {
    local d="$1" mem="$2"
    wait_gpu
    for q in gtx980ti rtx; do
        if [[ "$q" == "rtx" && "$QUALITY_CAP" == "gtx980ti" ]]; then
            echo "skip rtx (quality cap after crash step-down)" | tee "$d/skip-rtx.txt"
            continue
        fi
        if [[ "$DRY" -eq 1 ]]; then
            echo "+ $VISUAL --offscreen --bench --quality $q"
            continue
        fi
        ig "$mem" --quality "$q" --bench --approach KJFK:04R --cache-dir "$KJFK" \
            | tee "$d/bench-$q.txt"
        grep -E '^SUMMARY  s376 --bench' "$d/bench-$q.txt" | tee -a "$d/SUMMARY-LINE.txt" || true
    done
    append_summary "03 bench" \
        "result: PASS" \
        "gtx980ti: \`$d/bench-gtx980ti.txt\`" \
        "rtx: \`$d/bench-rtx.txt\`" \
        "paste: \`$d/SUMMARY-LINE.txt\`"
}

record_approach() {
    local d="$1" mem="$2" tod="$3" stem="$4"
    local fps="${5:-30}"
    local extra=()
    local rwy="04R"
    if [[ "${6:-}" == "demo" ]]; then
        extra+=(--demo kjfk --six-mile-final --start 6nm)
    elif [[ "${6:-}" == "demo13l" ]]; then
        extra+=(--demo kjfk --rwy 13L --six-mile-final --start 6nm)
        rwy="13L"
    elif [[ "${6:-}" == "demowet" ]]; then
        extra+=(--demo kjfk --six-mile-final --start 6nm --wet)
    fi
    wait_gpu
    local front="$d/${stem}-front.mp4"
    local wrap="$d/${stem}-wrap.mp4"
    if command -v ffmpeg >/dev/null 2>&1; then
        ig "$mem" --cache-dir "$KJFK" --quality gtx980ti --tod "$tod" \
            --channel mid_left --approach "KJFK:$rwy" --start-agl 2000 --gs 3.0 \
            --kts 140 --fps "$fps" "${extra[@]}" --record "$front"
        ig "$mem" --cache-dir "$KJFK" --quality gtx980ti --tod "$tod" \
            --channel all --approach "KJFK:$rwy" --start-agl 2000 --gs 3.0 \
            --kts 140 --fps "$fps" "${extra[@]}" --record "$wrap"
        append_summary "$stem" \
            "result: PASS" \
            "front: \`$front\`" \
            "wrap: \`$wrap\`" \
            "ffmpeg: yes"
    else
        ig "$mem" --cache-dir "$KJFK" --quality gtx980ti --tod "$tod" \
            --channel mid_left --approach "KJFK:$rwy" --start-agl 2000 --gs 3.0 \
            --kts 140 --fps "$fps" "${extra[@]}" --record "$d/${stem}-front.png"
        ig "$mem" --cache-dir "$KJFK" --quality gtx980ti --tod "$tod" \
            --channel all --approach "KJFK:$rwy" --start-agl 2000 --gs 3.0 \
            --kts 140 --fps "$fps" "${extra[@]}" --record "$d/${stem}-wrap.png"
        echo "ffmpeg missing — kept PNG frames" | tee "$d/NO-FFMPEG.txt"
        append_summary "$stem" \
            "result: PASS" \
            "ffmpeg: missing — PNG frames kept under \`$d\`" \
            "note: install ffmpeg later to mux; queue never uses sudo"
    fi
}

step_video_day() {
    record_approach "$1" "$2" 12 "video-day"
}

step_video_night() {
    record_approach "$1" "$2" 3 "video-night"
}

step_demo_kjfk_day() {
    # Live 60 Hz KJFK 04R demo (day). Repeat so Joey has a second take.
    record_approach "$1" "$2" 12 "demo-kjfk-day" 60 demo
    record_approach "$1" "$2" 12 "demo-kjfk-day-b" 60 demo
    record_approach "$1" "$2" 12 "demo-kjfk-13l" 60 demo13l
    record_approach "$1" "$2" 19 "demo-kjfk-dusk-wet" 60 demowet
}

step_demo_kjfk_night() {
    record_approach "$1" "$2" 3 "demo-kjfk-night" 60 demo
    record_approach "$1" "$2" 3 "demo-kjfk-night-b" 60 demo
}

step_bench_repeat() {
    local d="$1" mem="$2"
    wait_gpu
    for q in gtx980ti rtx; do
        if [[ "$q" == "rtx" && "$QUALITY_CAP" == "gtx980ti" ]]; then
            continue
        fi
        for tod in 12 3; do
            if [[ "$DRY" -eq 1 ]]; then
                echo "+ $VISUAL --offscreen --bench --quality $q --tod $tod --demo kjfk"
                continue
            fi
            ig "$mem" --quality "$q" --bench --demo kjfk --tod "$tod" --cache-dir "$KJFK" \
                | tee "$d/bench-$q-tod$tod.txt"
            grep -E '^SUMMARY  s376 --bench' "$d/bench-$q-tod$tod.txt" \
                | tee -a "$d/SUMMARY-LINE.txt" || true
        done
    done
    append_summary "11 bench-repeat" \
        "result: PASS" \
        "day+night × gtx980ti/rtx under \`$d\`" \
        "paste: \`$d/SUMMARY-LINE.txt\`"
}

step_skyline_water() {
    local d="$1" mem="$2"
    wait_gpu
    # Manhattan skyline (NW of JFK), Jamaica Bay water, terminals on short final.
    local tod hdg
    for tod in 12 3; do
        for hdg in 300 310 320; do
            shot "$mem" "$d/shots/kjfk_manhattan_hdg${hdg}_tod${tod}_front.ppm" \
                --cache-dir "$KJFK" --at KJFK --alt 2500 --hdg "$hdg" --tod "$tod" \
                --channel mid_left --quality gtx980ti
        done
        shot "$mem" "$d/shots/kjfk_manhattan_hdg310_tod${tod}_wrap.ppm" \
            --cache-dir "$KJFK" --at KJFK --alt 2500 --hdg 310 --tod "$tod" \
            --channel all --quality gtx980ti
        shot "$mem" "$d/shots/kjfk_04r_8nm_tod${tod}_front.ppm" \
            --cache-dir "$KJFK" --at KJFK:04R:8 --tod "$tod" \
            --channel mid_left --quality gtx980ti
        shot "$mem" "$d/shots/kjfk_04r_2.99nm_tod${tod}_front.ppm" \
            --cache-dir "$KJFK" --at KJFK:04R:2.99 --tod "$tod" \
            --channel mid_left --quality gtx980ti
        shot "$mem" "$d/shots/kjfk_04r_0.6nm_tod${tod}_front.ppm" \
            --cache-dir "$KJFK" --at KJFK:04R:0.6 --tod "$tod" \
            --channel mid_left --quality gtx980ti
        shot "$mem" "$d/shots/kjfk_04r_0.15nm_tod${tod}_front.ppm" \
            --cache-dir "$KJFK" --at KJFK:04R:0.15 --tod "$tod" \
            --channel mid_left --quality gtx980ti
        shot "$mem" "$d/shots/kjfk_13l_6nm_tod${tod}_front.ppm" \
            --cache-dir "$KJFK" --at KJFK:13L:6 --tod "$tod" \
            --channel mid_left --quality gtx980ti
    done
    shot "$mem" "$d/shots/kjfk_13l_6nm_tod19_wet_front.ppm" \
        --cache-dir "$KJFK" --at KJFK:13L:6 --tod 19 --wet \
        --channel mid_left --quality gtx980ti
    shot "$mem" "$d/shots/kjfk_12k_hdg036_tod12_front.ppm" \
        --cache-dir "$KJFK" --at KJFK --alt 12000 --hdg 036 --tod 12 \
        --channel mid_left --quality gtx980ti
    append_summary "12 skyline-water" \
        "result: PASS" \
        "shots: \`$d/shots\`" \
        "best frames: \`$d/shots/kjfk_manhattan_hdg310_tod12_front.png\`" \
        "\`$d/shots/kjfk_manhattan_hdg310_tod3_front.png\`" \
        "\`$d/shots/kjfk_04r_2.99nm_tod12_front.png\`" \
        "\`$d/shots/kjfk_04r_0.15nm_tod12_front.png\`"
}

step_fetch_kjfk_far() {
    local d="$1" mem="$2"
    if [[ "$DRY" -eq 0 ]]; then
        need_disk 20 "$(dirname "$KJFK")" || return 1
    fi
    # Resume: completed lodN/tY_X tiles are skipped. Extends lod1/lod2 to 74 km.
    fetch_pack "$mem" KJFK "$KJFK" nys \
        --near-m 1 --near-dem-m 1 --near-km 8 --approach-nm 12 \
        --mid-m 8 --mid-dem-m 8 --mid-km 40 \
        --far-m 32 --far-dem-m 32 --radius-km 74
    doctor_cache 4G "$KJFK" "$d/doctor.txt"
    local tiles bytes
    tiles="$(count_tiles "$KJFK")"
    bytes="$(dir_size "$KJFK")"
    append_summary "06 fetch-kjfk-far" \
        "result: PASS" \
        "cache: \`$KJFK\`" \
        "tiles: ${tiles:-?}  size: ${bytes:-?}" \
        "doctor: \`$d/doctor.txt\`" \
        "disk_free_gb: $(disk_avail_gb "$KJFK")"
}

step_fetch_kjfk_near() {
    local d="$1" mem="$2"
    local dest="$ROOT/cache/KJFK-near"
    if [[ "$DRY" -eq 0 ]]; then
        need_disk 40 "$(dirname "$dest")" || return 1
    fi
    # 0.3 m NYS/NYC near-field from data/regions/kjfk-near.toml. Resume.
    fetch_pack "$mem" KJFK "$dest" nys \
        --near-m 0.3 --near-dem-m 1 --near-km 3 --approach-nm 3 \
        --mid-m 1.2 --mid-dem-m 1 --mid-km 8 \
        --far-m 4.8 --far-dem-m 8 --radius-km 8
    doctor_cache 4G "$dest" "$d/doctor.txt"
    append_summary "07 fetch-kjfk-near" \
        "result: PASS" \
        "cache: \`$dest\`" \
        "plan: \`data/regions/kjfk-near.toml\`" \
        "doctor: \`$d/doctor.txt\`" \
        "size: $(dir_size "$dest")" \
        "disk_free_gb: $(disk_avail_gb "$dest")"
}

step_review_after_fetch() {
    local d="$1" mem="$2"
    local before="$OUT/02-review-now/shots"
    wait_gpu
    capped "$mem" "$REVIEW" --cache "$KJFK" --out "$d/shots" --bin "$VISUAL" \
        --quality gtx980ti --shots-only --wait 3
    contact_sheet "$before" "$d/shots" "$d/contact-02-vs-08.png"
    append_summary "08 review-after-fetch" \
        "result: PASS" \
        "shots: \`$d/shots\`" \
        "contact sheet: \`$d/contact-02-vs-08.png\`" \
        "best frames: \`$d/shots/kjfk_04r_2.99nm_tod12_front.png\`" \
        "\`$d/shots/kjfk_04r_0.6nm_tod12_front.png\`" \
        "\`$d/shots/kjfk_04r_0.15nm_tod12_front.png\`"
}

kewr_shots() {
    local d="$1" mem="$2" cache="$3"
    local rwy nm tod
    for rwy in 04R 22L; do
        for nm in 4 1 0; do
            for tod in 12 19 3; do
                shot "$mem" "$d/shots/kewr_${rwy}_${nm}nm_tod${tod}_front.ppm" \
                    --cache-dir "$cache" --at "KEWR:${rwy}:${nm}" --tod "$tod" \
                    --channel mid_left --quality gtx980ti
            done
        done
    done
}

step_fetch_kewr() {
    local d="$1" mem="$2"
    local dest="$ROOT/cache/KEWR"
    if [[ "$DRY" -eq 0 ]]; then
        need_disk 40 "$(dirname "$dest")" || return 1
    fi
    # NJOGIS is not a live ImageServer — NAIP + 3DEP is the unattended pull.
    fetch_pack "$mem" KEWR "$dest" naip \
        --near-m 1 --near-dem-m 1 --near-km 8 --approach-nm 12 \
        --mid-m 8 --mid-dem-m 8 --mid-km 40 \
        --far-m 32 --far-dem-m 32 --radius-km 74
    doctor_cache 4G "$dest" "$d/doctor.txt"
    kewr_shots "$d" 12G "$dest"
    append_summary "13 fetch-kewr (opt-in)" \
        "result: PASS" \
        "cache: \`$dest\`  size: $(dir_size "$dest")" \
        "doctor: \`$d/doctor.txt\`" \
        "shots: \`$d/shots\`" \
        "best frames: \`$d/shots/kewr_04R_1nm_tod12_front.png\`" \
        "\`$d/shots/kewr_22L_1nm_tod12_front.png\`"
}

us_rwy() {
    case "$1" in
        KLAS) echo 26L ;;
        KSFO) echo 28L ;;
        KLAX) echo 25L ;;
        KBOS) echo 04R ;;
        KSEA) echo 16L ;;
        KIAD) echo 01R ;;
        KMCO) echo 18R ;;
        KMIA) echo 09 ;;
        *) echo 04R ;;
    esac
}

us_img() {
    case "$1" in
        KSEA) echo kingco ;;
        KSFO) echo sanmateo ;;
        *) echo naip ;;
    esac
}

step_fetch_us_next() {
    local d="$1" mem="$2"
    local icao dest img rwy
    for icao in KLAS KSFO KLAX KBOS KSEA KIAD KMCO KMIA; do
        dest="$ROOT/cache/$icao"
        if [[ "$DRY" -eq 0 ]]; then
            if ! need_disk 300 "$(dirname "$dest")"; then
                echo "stop US-next at $icao — free disk below 300 GB" | tee "$d/STOP-DISK.txt"
                append_summary "14 fetch-us-next (opt-in)" \
                    "stopped before $icao: free disk below 300 GB" \
                    "log: \`$d/STOP-DISK.txt\`"
                return 0
            fi
        fi
        img="$(us_img "$icao")"
        rwy="$(us_rwy "$icao")"
        fetch_pack "$mem" "$icao" "$dest" "$img" \
            --near-m 1 --near-dem-m 1 --near-km 8 --approach-nm 8 \
            --mid-m 8 --mid-dem-m 8 --mid-km 40 \
            --far-m 32 --far-dem-m 32 --radius-km 74
        doctor_cache 4G "$dest" "$d/doctor-$icao.txt"
        shot 12G "$d/shots/${icao}_${rwy}_4nm_tod12_front.ppm" \
            --cache-dir "$dest" --at "${icao}:${rwy}:4" --tod 12 --channel mid_left
        shot 12G "$d/shots/${icao}_${rwy}_1nm_tod12_front.ppm" \
            --cache-dir "$dest" --at "${icao}:${rwy}:1" --tod 12 --channel mid_left
        shot 12G "$d/shots/${icao}_${rwy}_0nm_tod12_front.ppm" \
            --cache-dir "$dest" --at "${icao}:${rwy}:0" --tod 12 --channel mid_left
        [[ "$DRY" -eq 0 ]] && echo "$icao PASS $(dir_size "$dest")" >>"$d/airports.txt"
    done
    append_summary "14 fetch-us-next (opt-in)" \
        "result: PASS" \
        "order: KLAS, KSFO, KLAX, KBOS, KSEA, KIAD, KMCO, KMIA" \
        "log: \`$d/airports.txt\`" \
        "shots: \`$d/shots\`" \
        "disk_free_gb: $(disk_avail_gb "$ROOT/cache")"
}

# --- main ---

if [[ "$DRY" -eq 0 ]]; then
    init_summary
    load_limits
    detect_unclean
    journal_scan
    start_watchers
    write_status
fi

trap 'stop_watchers' EXIT

for s in "${STEPS[@]}"; do
    run_step "$(step_nn "$s")" "$(step_name "$s")" "$(step_mem "$s")"
done

if [[ "$DRY" -eq 0 ]]; then
    rm -f "$OUT/current"
    write_status
    echo "idle $(date -u +%Y-%m-%dT%H:%M:%SZ)" >>"$OUT/queue.log"
fi
echo "hangar-queue finished  status=$STATUS  summary=$SUMMARY"
