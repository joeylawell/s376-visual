# Free elevation and imagery

RSI CERTS tiles and `.nxt` textures are proprietary and are not used.
s376-visual streams or caches standard formats.

## Imagery (draped on the globe)

| Source | How we use it | Notes |
|---|---|---|
| [Esri World Imagery](https://www.arcgis.com/home/item.html?id=10df2279f9684e4a9f6a7f08febac2a9) (default earth file) | osgEarth XYZ, spherical mercator | Attribution required. Fine for development; check Esri terms before a public show |
| OpenStreetMap standard tiles | osgEarth XYZ | [Tile usage policy](https://operations.osmfoundation.org/policies/tiles/) — do **not** point a 60 Hz IG at `tile.openstreetmap.org` in production. Host your own tile cache |
| Sentinel-2 L2A (Copernicus) | Local GeoTIFF / MBTiles / WMTS | Best free true-colour around European airports (EGLL, EDDF, LEBL). Copernicus open licence |
| [NAIP](https://naip-usdaonline.hub.arcgis.com/) | Tiled ImageServer export + local BC7 | USDA ~0.6 m native; defaults pull **native** 0.60 m on the ARP + 12 nm finals, 2.4 m mid, 9.6 m far (4000 px tiles) |
| [NYS GIS Latest ortho](https://orthos.its.ny.gov/) | MapServer export (`--imagery nys`; auto at KJFK) | Public ~0.5 ft/px over NYC/LI. Defaults pull **native 0.15 m** (more tiles; disk is free) |
| USGS NAIP Plus / HRO | ImageServer export (`--imagery hro`) | Optional higher-tier over NAIP (US public domain) |
| osgEarth ReadyMap | TMS, if the public endpoint is still up | Historical osgEarth demo. Treat as optional |

## Per-airport imagery (sharper of public vs Raster-local)

See **[IMAGERY_SOURCES.md](IMAGERY_SOURCES.md)** for the approved
public-source table (NYS / NJOGIS / MassGIS / Loudoun / Clark /
LA County / OCPA / MDC / KingCo / San Mateo / Hessen DOP20 / EA VAP /
ICGC / IGM / IDECA) and the Raster-fallback rule (KRNO / SKRG / VNKT).

Pick whichever is sharper. Public sources only in this table. When the
free source is weaker or missing, use a **local** Raster airport folder
via `s376-cache import-raster` — never commit those files.

| ICAO | Best free public source | Native | Policy |
|---|---|---|---|
| KJFK | NYS ITS 2024 orthos / NYC 2024 | 0.15 m | public (`--imagery nys`) |
| KEWR | NJOGIS 2020 30 cm (`s3://njogis-imagery/2020` COG) | 0.30 m | public; download COGs then `import-raster` |
| KBOS | MassGIS 2023 15 cm | 0.15 m | public; WMTS / download then `import-raster` |
| KIAD | Loudoun COLOR_2025 30 cm | 0.30 m | public; download then `import-raster` |
| KLAS | Clark County cached imagery | ~0.15 m | public; download then `import-raster` |
| KLAX | LA County 4-inch | 0.10 m | public; download then `import-raster` |
| KMCO | OCPA Aerials2025 | ~0.15 m | public; download then `import-raster` |
| KMIA | Miami-Dade MDCImagery | ~0.15 m | public; download then `import-raster` |
| KSEA | King County KingCo_Aerial_2023 | ~0.08 m (3 in west) | public live pull (`--imagery kingco`) |
| KSFO | San Mateo 2022 ImageServer 15 cm | 0.15 m | public live pull (`--imagery sanmateo`) |
| EDDF | Hessen DOP20 (DL-DE/Zero) | 0.20 m | public; download then `import-raster` |
| EGLL | EA Vertical Aerial Photography (OGL; coverage to check) | ~0.25 m | public; confirm coverage, then `import-raster` |
| LEBL | ICGC ortofoto 2024 25 cm / IGN PNOA | 0.25 m | public; download then `import-raster` |
| SEQM | IGM Ecuador WMS | ~0.30 m | public; download then `import-raster` |
| SKBO | IDECA Bogotá 2025 | ~0.15 m | public; download then `import-raster` |
| KRNO | USDA NAIP (~0.6 m) — only free source | 0.60 m | either (NAIP roughly matches Raster) |
| SKRG | *(no verified public source)* | — | **Raster-local** `import-raster` |
| VNKT | Sentinel-2 10 m only (worse than Raster) | 10 m | **Raster-local** `import-raster` |

```bash
# Local-only. DIR is on Joey's disk. Nothing is copied into git.
./build/s376-cache import-raster /path/to/Raster/VNKT --out cache/VNKT
./build/s376-cache import-raster /path/to/Raster/SKRG --icao SKRG --out cache/SKRG
./build/s376-cache import-raster DIR --dry-run   # scan only; lists opaque vs decodable
```

`import-raster` reads GeoTIFF / JPEG2000 / JPEG+world / PNG+world that
GDAL can open. If the folder only has proprietary airport-pack files,
it exits non-zero and prints what it saw plus what it needs. It never
writes into the source directory. Packed output stays under `cache/`
(gitignored).

Live `s376-cache fetch --imagery auto` uses this table. SKRG / VNKT
refuse a worse public pull and tell you to `import-raster`.

The default `data/earth/s376.earth` points at Esri World Imagery so a
machine with osgEarth and a network gets a real-looking globe on the
first run. Swap the image layer for a local MBTiles tree when you go
offline or need airport-grade resolution.

## Elevation

| Source | Resolution | Notes |
|---|---|---|
| [Copernicus GLO-30](https://spacedata.copernicus.eu/collections/copernicus-digital-elevation-model) | 30 m | Preferred global DEM. Open Copernicus licence |
| [AWS Terrain Tiles](https://registry.opendata.aws/terrain-tiles/) (Mapzen / Amazon) | ~30 m, terrarium or quantized-mesh | Easy XYZ/TMS |
| SRTM 1 arc-second | 30 m | Void-filled copies via USGS / CGIAR. Weaker at high latitude |
| A local GeoTIFF | whatever you clip | Point the earth file `<elevation driver="gdal">` at it |

Without osgEarth the IG still has a local DEM: a portable `.s376dem`
grid (row-major metres MSL) plus a BC1/DXT1 DDS drape. `s376-cache
synth-ksea` writes a 25 km Sea-Tac box in a few hundred KiB. Point
`s376-cache from-geotiff` at a Copernicus GLO-30 clip (and optional
NAIP / Sentinel-2 GeoTIFF) for a real region. Disk is not a constraint:
a finished pack may exceed Raster's 857 GB CERTS farm if quality needs
it. The 980 Ti RAM/VRAM pager still is.

With osgEarth, add an elevation layer to `s376.earth` and the same
camera path rides the streamed mesh.

## Licences and attribution

| Product | Licence | How we cite it |
|---|---|---|
| Copernicus GLO-30 | [Copernicus DEM licence](https://spacedata.copernicus.eu) (© European Union) | `ATTRIBUTION.txt` in each packed cache |
| USGS 3DEP | US government work, public domain | same |
| USDA NAIP (via USGS National Map) | US government work, public domain | same |
| NYS GIS Program Office ortho | public NYS ortho (orthos.its.ny.gov) | same |
| USGS NAIP Plus / HRO | US government work, public domain | same |
| EOX Sentinel-2 cloudless | [CC BY 4.0](https://s2maps.eu/) + Copernicus Sentinel | same |
| OurAirports runways.csv | public domain | airport TOML / `airport.hpp` |
| OpenStreetMap aeroway=* / building=* / highway=motorway\|trunk\|primary\|secondary | [ODbL](https://www.openstreetmap.org/copyright) | OSM taxi/apron/building/road ways; attribute OSM |
| Classic NYC skyline (easter egg) | stylized stand-ins | Art-directed pre-2001 Lower Manhattan (not surveyed). Public WTC-era silhouette only. Session-only, NYC geofence |
| Missiles keep-out (Classic only) | 40.7115 N, 74.0134 W | Public WTC site; 10 statute mile disk. Visual-only egg; no surveyed claim |
| Poly Haven 1K PBR (asphalt, concrete, grass, dirt) | [CC0 1.0](https://polyhaven.com/license) | `scripts/fetch-textures.sh` → `cache/textures/` (gitignored) |
| ambientCG (optional extra maps) | [CC0 1.0](https://docs.ambientcg.com/license/) | same cache dir |
| [stb_image_write](https://github.com/nothings/stb) v1.16 | public domain / MIT | `third_party/stb_image_write.h` — PNG from `--screenshot` / `--self-test-shot` |

Do **not** commit downloaded GeoTIFFs, JPEGs, or packed `cache/` trees.
`cache/` and `*.tif` are gitignored. `s376-cache fetch` writes
`ATTRIBUTION.txt` next to the tiles.

## Caching

```bash
# synthetic KSEA (no download, CI and laptops)
./build/s376-cache synth-ksea --out cache/ksea --lods

# real data: GLO-30 or USGS 3DEP + NAIP (CONUS) / Sentinel-2 cloudless
./build/s376-cache fetch KSEA --out cache/ksea
# native NAIP 0.60 m / 3DEP 1 m on 8 km + 12 nm finals, 50 km outer
./build/s376-cache fetch KSFO --out cache/ksfo
./build/s376-cache fetch EGLL --dem glo30 --imagery s2 --out cache/egll

# preview the public URLs without downloading
./build/s376-cache fetch KSEA --dry-run

# OSM taxi / apron / building ways (ODbL) — not committed.
# Writes osm.json then stream-bakes osm.bin + solids.bin. The IG loads those.
./build/s376-cache osm KSFO --out cache/KSFO
./build/s376-cache bake-osm cache/KJFK   # if osm.json already exists
# NYS ortho (KJFK auto); optional HRO; never Google/Bing/Esri scrape
./build/s376-cache fetch KJFK --imagery nys --out cache/KJFK

# optional CC0 close-up detail (never committed)
./scripts/fetch-textures.sh           # cache/textures/*.jpg
```

`fetch` packs a LOD pyramid: **native** ortho (~8 km ARP + 12 nm
finals, BC7 + mips) near the field, 4× mid (~40 km, BC1), 16× far
(~50 km). Root `elev.s376dem` is the finest LOD so HAT still works.
Disk is free — quality and accuracy win over storage. Resident RAM /
VRAM on the 980 Ti still apply (see [QUALITY.md](QUALITY.md)).

Then set `render.cache_dir = "cache/ksea"` (or `--cache-dir`) and, with
osgEarth, `render.earth_file = "data/earth/ksea.earth"`. That earth file
reads `cache/ksea/elev.tif`, `imagery.tif`, and `osm.geojson`. Without
osgEarth the IG drapes the same cache on the procedural globe.

osgEarth's own file cache is separate and is enough for a hangar box
that has the library.

## What we will not ship

- RSI / CAE binaries, textures, CERTS tiles, `.nx3` extracts
- Captured host datagrams
- Paywalled imagery (most Maxar / Google / Bing tilesets)
