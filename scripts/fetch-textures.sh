#!/usr/bin/env bash
# Download CC0 detail textures (ambientCG / Poly Haven) into cache/textures/.
# Never commit those files — /cache/textures/ is gitignored.
# Usage: scripts/fetch-textures.sh [out-dir]
set -euo pipefail

OUT="${1:-cache/textures}"
mkdir -p "$OUT"

# Direct 1K JPGs so we do not need unzip. CC0; hangar / local cache only.
# curl -g so any future [ ] in a URL cannot glob.
pull() {
    local dest="$1"
    local url="$2"
    if [[ -s "$dest" ]]; then
        echo "have $(basename "$dest") ($(wc -c < "$dest") bytes)"
        return 0
    fi
    echo "GET $url"
    curl -fL --retry 5 --retry-delay 2 -g -o "$dest.part" "$url"
    mv "$dest.part" "$dest"
    echo "wrote $dest ($(wc -c < "$dest") bytes)"
}

pull "$OUT/asphalt.jpg" \
    "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/asphalt_02/asphalt_02_diff_1k.jpg"
pull "$OUT/concrete.jpg" \
    "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/concrete_layers_02/concrete_layers_02_diff_1k.jpg"
pull "$OUT/grass.jpg" \
    "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/aerial_grass_rock/aerial_grass_rock_diff_1k.jpg"
pull "$OUT/dirt.jpg" \
    "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/brown_mud_leaves_01/brown_mud_leaves_01_diff_1k.jpg"

cat > "$OUT/ATTRIBUTION.txt" <<'EOF'
CC0 detail textures (not part of the s376-visual git tree)

- asphalt_02, concrete_layers_02, aerial_grass_rock, brown_mud_leaves_01
  Poly Haven, CC0 1.0  https://polyhaven.com
- Fallback / extra maps may come from ambientCG, CC0 1.0  https://ambientcg.com

The IG uses these as optional close-up detail under NAIP/Sentinel drapes
and on asphalt/concrete. Shader hash noise is the offline fallback.
EOF

echo "textures in $OUT"
ls -l "$OUT"
