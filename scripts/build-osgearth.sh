#!/usr/bin/env bash
# Build osgEarth 3.7.x from source into /opt/osgearth and rebuild s376-visual
# against it. Ubuntu 24.04 / Mint 22 do not package osgEarth.
#
# Uses apt OSG 3.6.5 (compatibility GL). Disables FASTDXT / ImGui / examples
# so the result stays GL 3.3-friendly for Maxwell (GTX 980 Ti) and the
# hangar RTX 5060 Ti.
#
# Never talks to 10.1.95.0/24, 192.168.173.0/24, or UDP 3340/3341.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${S376_OSGEARTH_PREFIX:-/opt/osgearth}"
SRC="${S376_OSGEARTH_SRC:-$HOME/src/osgearth}"
TAG="${S376_OSGEARTH_TAG:-osgearth-3.7.2}"
JOBS="$(nproc)"
DRY=0

usage() {
    cat <<EOF
usage: scripts/build-osgearth.sh [--prefix DIR] [--src DIR] [--tag TAG] [-j N] [--dry-run]

  --prefix DIR   install prefix (default: $PREFIX)
  --src DIR      clone / build tree (default: $SRC)
  --tag TAG      git tag or branch (default: $TAG)
  -j N           build parallelism
  --dry-run      print commands only
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix) PREFIX="${2:?}"; shift ;;
        --src) SRC="${2:?}"; shift ;;
        --tag) TAG="${2:?}"; shift ;;
        -j) JOBS="${2:?}"; shift ;;
        --dry-run) DRY=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
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

as_root() {
    if [[ "$(id -u)" -eq 0 ]]; then
        run "$@"
    elif command -v sudo >/dev/null 2>&1; then
        run sudo "$@"
    else
        echo "need root to install into $PREFIX" >&2
        exit 1
    fi
}

DEPS=(
    git cmake g++
    libopenscenegraph-dev libgl-dev libglew-dev
    libgdal-dev libgeos-dev libproj-dev
    libprotobuf-dev protobuf-compiler
    libtinyxml-dev libcurl4-openssl-dev libsqlite3-dev
)

if command -v sudo >/dev/null 2>&1 && [[ "$(id -u)" -ne 0 ]]; then
    run sudo apt-get update
    run sudo apt-get install -y --no-install-recommends "${DEPS[@]}"
else
    run apt-get update
    run apt-get install -y --no-install-recommends "${DEPS[@]}"
fi

if [[ "$DRY" -eq 1 ]]; then
    echo "+ git clone --depth 1 --branch $TAG https://github.com/gwaldron/osgearth.git $SRC"
else
    if [[ ! -d "$SRC/.git" ]]; then
        mkdir -p "$(dirname "$SRC")"
        git clone --depth 1 --branch "$TAG" https://github.com/gwaldron/osgearth.git "$SRC"
    fi
fi

run cmake -S "$SRC" -B "$SRC/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DOSGEARTH_BUILD_TOOLS=OFF \
    -DOSGEARTH_BUILD_EXAMPLES=OFF \
    -DOSGEARTH_BUILD_TESTS=OFF \
    -DOSGEARTH_BUILD_IMGUI_NODEKIT=OFF \
    -DOSGEARTH_ENABLE_FASTDXT=OFF
run cmake --build "$SRC/build" -j"$JOBS"
as_root cmake --install "$SRC/build"
if [[ -d /etc/ld.so.conf.d ]]; then
    if [[ "$DRY" -eq 1 ]]; then
        echo "+ echo $PREFIX/lib > /etc/ld.so.conf.d/osgearth.conf && ldconfig"
    else
        echo "$PREFIX/lib" | as_root tee /etc/ld.so.conf.d/osgearth.conf >/dev/null
        as_root ldconfig
    fi
fi

run cmake -S "$ROOT" -B "$ROOT/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_PREFIX_PATH="$PREFIX"
run cmake --build "$ROOT/build" -j"$JOBS"

if [[ "$DRY" -eq 1 ]]; then
    echo "+ $ROOT/build/s376-doctor -c $ROOT/config/s376-visual.toml"
else
    "$ROOT/build/s376-doctor" -c "$ROOT/config/s376-visual.toml" || true
fi

echo "osgEarth installed at $PREFIX. Re-ran s376-visual cmake with CMAKE_PREFIX_PATH."
echo "Point render.earth_file at data/earth/ksea.earth after s376-cache has written cache/ksea."
