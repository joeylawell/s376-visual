#!/usr/bin/env bash
# One-command Linux Mint 22 / Ubuntu 24.04 setup for s376-visual.
# Installs apt deps, builds with g++, runs tests, packs a synth KSEA cache,
# and writes desktop launchers for --demo (mid_left) and the laptop
# 3-channel config.
#
# Never talks to 10.1.95.0/24, 192.168.173.0/24, or UDP 3340/3341.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

SKIP_APT=0
NO_DESKTOP=0
DRY=0
JOBS="$(nproc)"

usage() {
    cat <<'EOF'
usage: scripts/setup-mint.sh [--skip-apt] [--no-desktop] [--dry-run] [-j N]

  --skip-apt      do not apt-get (packages already installed)
  --no-desktop    skip ~/.local/share/applications and ~/Desktop launchers
  --dry-run       print what would run and exit 0
  -j N            build parallelism (default: nproc)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-apt) SKIP_APT=1 ;;
        --no-desktop) NO_DESKTOP=1 ;;
        --dry-run) DRY=1 ;;
        -j)
            JOBS="${2:?}"
            shift
            ;;
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

run() {
    if [[ "$DRY" -eq 1 ]]; then
        printf '+'
        printf ' %q' "$@"
        printf '\n'
        return 0
    fi
    "$@"
}

APT_PKGS=(
    git cmake g++ pkg-config
    libopenscenegraph-dev libgl-dev libglew-dev libgdal-dev
    libx11-dev libglfw3-dev xvfb
)

if [[ "$SKIP_APT" -eq 0 ]]; then
    if command -v sudo >/dev/null 2>&1 && [[ "$(id -u)" -ne 0 ]]; then
        run sudo apt-get update
        run sudo apt-get install -y --no-install-recommends "${APT_PKGS[@]}"
    else
        run apt-get update
        run apt-get install -y --no-install-recommends "${APT_PKGS[@]}"
    fi
fi

# Apt OSG 3.6.5 is a compatibility-profile GL 3.x stack. Stay off Core-only
# and GL 4+ so a GTX 980 Ti (Maxwell, GL 4.6 max) can run the same binary.
run cmake -S "$ROOT" -B "$ROOT/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++
run cmake --build "$ROOT/build" -j"$JOBS"

if [[ "$DRY" -eq 1 ]]; then
    echo "+ unset S376_ALLOW_SIM_LAN S376_ALLOW_LAN && ctest --test-dir $ROOT/build --output-on-failure"
    echo "+ $ROOT/build/s376-cache synth-ksea --out $ROOT/cache/ksea --lods"
    echo "+ $ROOT/build/s376-cache geojson KSEA --out $ROOT/cache/ksea"
    echo "+ $ROOT/build/s376-doctor -c $ROOT/config/s376-visual.toml"
else
    unset S376_ALLOW_SIM_LAN S376_ALLOW_LAN || true
    ctest --test-dir "$ROOT/build" --output-on-failure
    "$ROOT/build/s376-cache" synth-ksea --out "$ROOT/cache/ksea" --lods
    "$ROOT/build/s376-cache" geojson KSEA --out "$ROOT/cache/ksea"
    "$ROOT/build/s376-doctor" -c "$ROOT/config/s376-visual.toml" || true
fi

write_desktop() {
    local dest="$1"
    local name="$2"
    local exec_line="$3"
    local comment="$4"
    mkdir -p "$(dirname "$dest")"
    cat >"$dest" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=$name
Comment=$comment
Exec=$exec_line
Path=$ROOT
Terminal=true
Categories=Education;Game;
StartupNotify=false
EOF
    chmod +x "$dest" || true
}

if [[ "$NO_DESKTOP" -eq 0 ]]; then
    BIN="$ROOT/build/s376-visual"
    DEMO_EXEC="$BIN --demo --channel mid_left --cache-dir $ROOT/cache/ksea -c $ROOT/config/s376-visual.toml"
    CH3_EXEC="$BIN --demo --cache-dir $ROOT/cache/ksea -c $ROOT/config/channels-3.toml"
    APPDIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
    if [[ "$DRY" -eq 1 ]]; then
        echo "+ write $APPDIR/s376-visual-demo.desktop"
        echo "+ write $APPDIR/s376-visual-3ch.desktop"
    else
        write_desktop "$APPDIR/s376-visual-demo.desktop" \
            "s376-visual demo" "$DEMO_EXEC" \
            "KSEA 16L takeoff, one projector channel (mid_left)"
        write_desktop "$APPDIR/s376-visual-3ch.desktop" \
            "s376-visual 3-channel" "$CH3_EXEC" \
            "Laptop 3-channel alternative (front/left/right)"
        write_desktop "$APPDIR/s376-visual-desk.desktop" \
            "s376-visual 4-monitor desk" \
            "$BIN --demo --cache-dir $ROOT/cache/ksea -c $ROOT/config/desk-4monitor.toml" \
            "IOS on DP-4, L/F/R on DP-3 / HDMI-0 / DP-0"
        write_desktop "$APPDIR/s376-visual-weather.desktop" \
            "s376-visual weather demo" \
            "$BIN --demo-weather --channel mid_left --cache-dir $ROOT/cache/ksea -c $ROOT/config/s376-visual.toml" \
            "Type-124 rain / type-128 fog (host fields only)"
        if [[ -d "$HOME/Desktop" ]]; then
            write_desktop "$HOME/Desktop/s376-visual-demo.desktop" \
                "s376-visual demo" "$DEMO_EXEC" \
                "KSEA 16L takeoff, one projector channel (mid_left)"
            write_desktop "$HOME/Desktop/s376-visual-3ch.desktop" \
                "s376-visual 3-channel" "$CH3_EXEC" \
                "Left / front / right CompositeViewer"
        fi
        command -v update-desktop-database >/dev/null 2>&1 && \
            update-desktop-database "$APPDIR" >/dev/null 2>&1 || true
    fi
fi

echo "s376-visual setup ok. Demo: $ROOT/build/s376-visual --demo --channel mid_left"
echo "Optional osgEarth: scripts/build-osgearth.sh  (installs to /opt/osgearth)"
