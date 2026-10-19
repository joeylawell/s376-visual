# Quality tiers and GPU/CPU budgets

s376-visual must beat Raster XT4 in every way. **Accuracy is first**
(see [ACCURACY.md](ACCURACY.md)): never trade a FAA number, lighting
system, or geodetic conversion for looks or frame time.

**Disk is not a constraint.** The packed dataset may exceed Raster's
857 GB CERTS farm when quality improves — the owner will fit new SSDs
in the visual PCs. Quality and accuracy win over storage. Fetch the
highest published ortho at airports and approaches, and denser global
terrain; do not downsample to save disk.

The remaining hard constraint is the **runtime** box — an
**i7-5930K + GTX 980 Ti 6 GB** (Maxwell, NVIDIA 580, OpenGL 3.3
compatibility) and **16 GB RAM** at 60 Hz per channel. Resident
`ram_mb` / `vram_mb` still apply; a larger pack is fine, a larger
resident set is not. The hangar RTX 5060 Ti is the `rtx` preset.

## Presets

| | `gtx980ti` (default) | `rtx` |
|---|---|---|
| Target GPU | GTX 980 Ti 6 GB Maxwell | RTX 5060 Ti / similar |
| Assumed system RAM | 16 GB | 16 GB+ |
| `render.ram_mb` | **2048** | 3072 |
| `render.vram_mb` | **5120** (leave ~1 GB for the desktop / HUD) | 10240 |
| `render.mesh_n` | 96 samples / tile | 128 |
| MSAA | 0 (TAA) | 4+ |
| Aniso | 16 | 16 |
| HDR / ACES | on | on |
| Atmosphere / water / markings / detail / buildings / SSAO | on | on |
| Bloom / FXAA / TAA / clustered / lod_fade / filmic | on | on |
| Shadows | on (2-cascade 512+512, ±400 / ±2200 m) | on (2-cascade 1024+1024) |
| Texture format | BC1 (DXT1) far, BC7 near | same |
| CPU decode to RGBA | **never** | never |

Override any field in TOML after `quality = ...`. `custom` skips the
preset clamp. Each quality lever is independently switchable
(`render.atmosphere`, `render.water`, `render.detail_textures`,
`render.buildings`, `render.markings`, `render.bloom`, `render.filmic`,
`render.fxaa`, `render.msaa`, `render.shadows`). Shot CLI:
`--tod HOURS` (host type 124) and `--rwy-cond N` (0 dry / 1 damp / 2 wet
/ ≥3 flooded).

## GTX 980 Ti per-feature GPU budget

One channel, 1920×1200, 60 Hz = **16.67 ms**. Hangar is one 980 Ti per
channel PC. Values are Maxwell ALU budgets (this box has no 980 Ti;
llvmpipe is a relative proxy only). Real `GL_TIME_ELAPSED` waits until
Joey's Mint PC is back.

| Feature | Toggle | GPU ms | What it draws |
|---|---|---|---|
| base (tiles + mesh + lights) | — | **6.00** | paged lod + official lights |
| atmosphere | `render.atmosphere` | **0.45** | Rayleigh/Mie sky, aerial haze from host visibility |
| water | `render.water` | **0.80** | Jamaica Bay / ocean from cover mask + chroma, Fresnel, glint, foam |
| detail | `render.detail_textures` | **0.45** | land-cover atlas under ortho (near field) |
| buildings | `render.buildings` | **1.00** | Official + OSM campus (800) + Queens/Brooklyn ring (2500) |
| runway PBR | `render.markings` | **0.35** | asphalt/concrete + rubber + wet + night HIRL sheen |
| bloom | `render.bloom` | **0.55** | Raster fog-halo on lights + city glow |
| filmic | `render.filmic` | **0.15** | lift/gamma/gain after ACES |
| TAA | `render.taa` | **0.40** | live-path temporal AA (stills stay TAA-off) |
| FXAA | `render.fxaa` | **0.20** | fallback if TAA drops |
| clustered | `render.clustered` | **0.60** | 32 closest FAA/OSM lights on the ground |
| shadows | `render.shadows` | **0.85** | 2-cascade sun CSM (512+512 on 980 Ti) |
| SSAO | `render.ssao` | **0.25** | cheap baked AO |
| lod fade | `render.lod_fade` | **0.15** | page-in fade, no pop |

`quality_gpu_budget_total_ms(true)` = **12.20 ms** (default hangar, shadows on).
`quality_gpu_budget_total_ms(false)` = **11.35 ms** (after autodrop shadows).
Both fit under 16.67 ms. Official runway/taxi coordinates are not changed.

`--bench` flies KJFK:04R (or `--approach`) and prints per-sample GPU ms
from `GL_TIME_ELAPSED` plus a budgeted pass table, then one pasteable
line:

`SUMMARY  s376 --bench quality=gtx980ti budget=12.20ms max=…ms@…nm avg=…ms dropped=none result=UNDER 60Hz=YES`

`gtx980ti` sets `render.auto_drop` so extras drop in this order if a
measured frame exceeds `gpu_budget_ms`: SSAO, shadows, bloom, FXAA,
TAA, lod_fade, detail, filmic, clustered. Water, markings, buildings,
and atmosphere stay on. `--offscreen` prefers a pbuffer so hangar
projectors stay dark (`scripts/hangar-review.sh`, `scripts/hangar-queue.sh`).

`--demo kjfk` flies the live 60 Hz KJFK 04R approach (NASR lights and
runways, day `--tod 12` / night `--tod 3`). Bare `--demo` stays the
in-process synth path for CI.

Night-light GPU impact on the 980 Ti (same point-sprite path):

| Change | GPU ms | Why |
|---|---|---|
| drop 60 m ARP lattice (~2200 Night pts) | **−0.15** | fewer sprites in the night pass |
| OSM/public-road street lamps (~400, cap 800) | in base | real roads, not a new draw |
| runway night asphalt sheen | **+0.05** | few ALU ops in existing `kLitColorFS` |

Net **−0.10 ms**. Budgeted total stays **12.20 ms**.

Far-field hangar frames (2.99 / 0.6 nm, `--tod 12`): globe-under-tiles
**+0.05** ALU (one 160×80 mesh, absorbed in the 6.00 ms base). Reserved
lod1/lod2 are cheap coarse meshes. Budgeted total stays **12.20 ms**.

## Why a 1 GB KJFK pack used 14 GB RAM

A packed `cache/KJFK` on the hangar box was about **1.6 GB on disk**
(lod0 ~1.0 GB in 14 subdirs, lod1 29.6 MB, `osm.json` 436 MB,
`osm.geojson` 150 MB, root `elev.s376dem` 590 KB). The IG used to:

1. Mesh every lod0 DEM at full 4000² (~16 M verts ≈ 512 MB/tile × 14).
2. Hillshade / decode BC tiles to RGB(A) (~48 MB/tile).
3. Slurp `osm.json` into a DOM for every public airport.

That peaked at **anon-rss 13.9 GB** and the kernel OOM-killed the IG
and the host app.

Runtime rules now:

- **Page** `lodN/tY_X` by distance and a coarse heading frustum.
  **Reserved lod1/lod2** inside 40 nm (74 km) cannot be starved by
  lod0 1 m tiles. A coarse tile is evicted only when lod0 **fully**
  covers it (overlap used to drop the 50 km lod2 and leave empty sky).
  The procedural globe stays under packed tiles (80 m inset) so the
  far field is never a sky-blue void while coarse pages in.
- **Subsample** each resident DEM to `mesh_n` (`DemGrid::load_subsampled`).
- **Upload DXT1 through OSG 3.6.** `dxt::read_dds` keeps the block
  payload. BC7 (BPTC) is transcoded block-by-block to DXT1 because
  apt OSG 3.6 does not treat `GL_COMPRESSED_RGBA_BPTC_UNORM` as
  compressed (`glTexImage2D` → `invalid enumerant`). No 48 MB RGBA
  inflate. `setUnRefImageDataAfterApply(true)` drops the CPU copy.
- **`osm.bin` + `solids.bin`.** `s376-cache osm` / `bake-osm` stream-parses
  Overpass JSON (no full slurp) into a compact `S376OSM1` sidecar
  (mesh-capped) and `S376SLD1` (`solids.bin`, every building). The
  IG never opens `osm.json`. Collision is the always-resident grid.
- Evict farthest lod0 tiles when `ram_mb` / `vram_mb` would be exceeded.

`s376-doctor` prints `rss` (peak / current / budget) and `cache-tiles`
(naive full-res mesh vs paged estimate). Target: **peak RSS under the
preset budget** on a 16 GB / 6 GB VRAM box.

```bash
./build/s376-cache bake-osm cache/KJFK   # if you already have osm.json
./build/s376-doctor --cache-dir cache/KJFK
# expect [pass] rss  peak < 2 GiB   and cache-tiles paged mesh << naive
```

## Imagery resolution (disk-unlimited tiers)

`s376-cache fetch` metres/px default to **0 = native for the chosen
source**. `max_export=2048` is the ImageServer cap (USGS 3DEP and NYS
return 500/504 on ~4k F32/JPG). More tiles, not blurrier pixels. A
2048 px tile at NYS 0.15 m/px is **307 m** on a side. Re-run fetch to
resume — completed `lodN/tY_X` tiles are skipped. `--plan` prints
every tile bbox and FAA coverage with no download. Do not upsample a
source finer than native unless the operator set a positive
`--near-m` (e.g. `--near-m 1` on NYS stays 1 m).

| LOD | Coverage | Imagery | DEM |
|---|---|---|---|
| 0 | **8 km** around the ARP + **12 nm** finals | native: NYS **0.15 m**, HRO **0.30 m**, NAIP **0.60 m**, S2 **10 m** | native: 3DEP **1 m**, GLO-30 **30 m** |
| 1 | **40 km** | 4× native | 2× native |
| 2 | **40 nm / 74 km** outer | 16× native | 8× native (never finer than native) |

| Source | Native | Default pull (near / mid / far) | Licence |
|---|---|---|---|
| USDA NAIP (USGS NAIPImagery) | ~0.6 m | **0.60 / 2.4 / 9.6 m** | US public domain |
| USGS NAIP Plus / HRO (`--imagery hro`) | ~0.3 m | **0.30 / 1.2 / 4.8 m** | US public domain |
| NYS GIS Latest ortho (`--imagery nys`, auto at KJFK) | ~0.15 m (0.5 ft) | **0.15 / 0.60 / 2.4 m** | NYS public ortho |
| Sentinel-2 cloudless | 10 m | **10 / 40 / 160 m** | CC BY 4.0 + Copernicus |
| USGS 3DEP | 1 m | **1 / 2 / 8 m** | US public domain |
| Copernicus GLO-30 | 30 m | **30 / 60 / 240 m** | Copernicus open |

`--imagery auto` uses the per-airport table in [SOURCES.md](SOURCES.md)
(NYS at KJFK, King County at KSEA, San Mateo at KSFO, NAIP at KRNO, …).
If the free source is weaker than Raster (SKRG, VNKT), fetch refuses
and `s376-cache import-raster` reads Joey's local airport folder.
No Google / Bing / Esri scrape.

The pack may be larger than Raster's 857 GB. The pager still evicts
when `ram_mb` / `vram_mb` would exceed the preset (2 GiB / 5 GiB on
`gtx980ti`). VRAM grows with how many lod0 tiles sit in the frustum,
not with how many tiles sit on disk.

## Ground detail

A generated 128² atlas (grass / asphalt / dirt / concrete quadrants)
is blended over the ortho with a **distance fade** (strong inside
~2.5 km, gone by ~8 km). Optional CC0 photos from Poly Haven /
ambientCG can be fetched into `cache/textures/` (gitignored) — the
runtime atlas does not require them. See [SOURCES.md](SOURCES.md).

## Screenshot viewport

`--screenshot` and `--record` use a non-MSAA FBO at the channel WxH. The slave
camera **does not inherit** the master's viewport (a 1920×1008
decorated window used to leave a ~7% black bar on 1920×1080 captures).
`--record` writes one RGB frame at a time (ffmpeg pipe or a PNG) so a
full 04R approach stays inside the 980 Ti `ram_mb` cap.

## Quality-pass frames (llvmpipe, `/tmp/kjfk-tiny` ~8 m)

Hangar 980 Ti / 1 m pack is offline. These are software-GL proxies.

| Pair | Before | After |
|---|---|---|
| 0.6 nm day | `kjfk_04r_0.6nm_quality_before.png` | `kjfk_04r_0.6nm_quality_after.png` |
| 2.99 nm day | `kjfk_04r_2.99nm_quality_before.png` | `kjfk_04r_2.99nm_quality_after.png` |
| atmosphere off/on | `kjfk_04r_0.6nm_atmo_off.png` | `kjfk_04r_0.6nm_quality_after.png` |
| water off/on | `kjfk_04r_0.6nm_water_off.png` | `kjfk_04r_0.6nm_quality_after.png` |
| detail off/on | `kjfk_04r_0.6nm_detail_off.png` | `kjfk_04r_0.6nm_quality_after.png` |
| runway dry/wet | `kjfk_04r_0.15nm_dry.png` | `kjfk_04r_0.15nm_wet.png` (`--rwy-cond 2`) |
| dusk | `kjfk_04r_0.6nm_dusk_before.png` (`--tod 19` was day) | `kjfk_04r_0.6nm_dusk.png` (`--tod 19`) |
| night | `kjfk_04r_0.6nm_night_before.png` | `kjfk_04r_0.6nm_night.png` (`--tod 3`) + `kjfk_04r_2.99nm_night.png` |
| night streets / 04R HIRL | `kjfk_04r_0.6nm_night.png` (60 m yellow lattice) | `kjfk_04r_0.6nm_night_after.png` |
| dusk streets / 04R HIRL | `kjfk_04r_0.6nm_dusk.png` (same lattice) | `kjfk_04r_0.6nm_dusk_after.png` |

Host type-124 TOD drives the **visual** sun elevation (day / dusk / night)
regardless of date and longitude. `--tod 19` is dusk; `--tod 3` is night
(navy sky, Raster-style light halos). `sun_ecef` (date + lon) stays as
the opt-in astronomy path: `render.solar_astronomy` or `--solar-astronomy`.

The 0.6 nm day lower half is Jamaica Bay in the 8 m test pack, not a
land-mask bleed over the pad (`land_coverage` keeps the field as land
and the short final as water). Water keeps 32 % of the ortho so the
pack does not paint-bucket into a slab. The left-horizon line is official 04L edge lights; the black
rectangle was those plus 0.11 m ALS pier-leg mesh seen edge-on.
Pier mesh removed; lights stay.
