#!/usr/bin/env bash
# Stage the same tree pack-windows.ps1 zips. Used to syntax-check the
# layout on Linux CI. Does not download OSG or imagery.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build}"
OUT="${2:-$ROOT/s376-visual-windows-layout}"
rm -rf "$OUT"
mkdir -p "$OUT/bin"
for n in s376-visual s376-cache s376-doctor s376-synth s376-raster-parity; do
    if [[ -x "$BUILD/Release/$n.exe" ]]; then
        cp "$BUILD/Release/$n.exe" "$OUT/bin/"
    elif [[ -x "$BUILD/$n" ]]; then
        cp "$BUILD/$n" "$OUT/bin/"
    fi
done
cp -R "$ROOT/config" "$OUT/config"
cp -R "$ROOT/data" "$OUT/data"
cp "$ROOT/README.md" "$ROOT/HANDOFF.md" "$OUT/"
if [[ -d "$ROOT/docs" ]]; then
    cp -R "$ROOT/docs" "$OUT/docs"
fi
# Channel PCs: one process per [[channels]] row. Count is TOML, not 3.
cat > "$OUT/CHANNEL-PCS.txt" <<'EOF'
s376-visual channel PCs (GTX 980 Ti target)
==========================================
One projector PC per enabled [[channels]] block in config/s376-visual.toml.
Default is four PCs (far_left, mid_left, mid_right, far_right). Do not
assume three. FOV, heading offset, and resolution are per-channel.

  s376-visual.exe -c config\s376-visual.toml --channel far_left --quality gtx980ti
  s376-visual.exe -c config\s376-visual.toml --channel mid_left --quality gtx980ti
  s376-visual.exe -c config\s376-visual.toml --channel mid_right --quality gtx980ti
  s376-visual.exe -c config\s376-visual.toml --channel far_right --quality gtx980ti

Default output 1920x1200. Override with resolution = "WxH" on that
[[channels]] row. reply_master = mid_left (only that PC sends type-200).
--bench KJFK:04R prints per-pass GPU ms (timer queries) on the hangar card.
Never copy Raster / RSI files or packed cache/ imagery into this zip.
EOF
if find "$OUT" -type f \( -iname '*.nxt' -o -iname '*.nxb' -o -iname '*.nx3' \
    -o -iname '*.nxs' -o -iname '*.nxo' -o -iname '*.tif' -o -iname '*.tiff' \
    -o -iname '*.jp2' \) | grep -q .; then
    echo "refusing imagery or Raster files in the pack" >&2
    exit 1
fi
echo "staged $OUT"
