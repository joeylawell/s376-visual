# Per-airport imagery (public sources only)

Pick the **sharper** of the free public source and that airport’s
Raster imagery. When the free source is weaker or missing, fall back
to Joey’s Raster airport folder on **his machine** via the local-only
importer — never commit those files.

```bash
# Local-only. DIR is on Joey's disk. Nothing is copied into git.
./build/s376-cache import-raster /path/to/Raster/VNKT --out cache/VNKT
./build/s376-cache import-raster /path/to/Raster/SKRG --icao SKRG --out cache/SKRG
./build/s376-cache import-raster DIR --dry-run
```

`import-raster` reads GeoTIFF / JPEG2000 / JPEG+world / PNG+world that
GDAL can open. If the Raster texture format cannot be decoded from
what is on disk, it exits non-zero and prints what it saw plus what
it needs. It never writes into the source directory. Packed output
stays under `cache/` (gitignored). `scripts/check-no-rsi.sh` must
still pass — no RSI data, textures, extracted imagery, CAE/RSI format
docs, or sample files.

Geometry and lights always come from official FAA NASR / AIP AD 2,
never from Raster. Imagery is only texture. When a photo disagrees
with official data (old painted threshold, extension, demolished
pavement), the correction layer is drawn over the packed tiles:
official surface, markings, displaced-threshold arrows/bars, and
EMAS, plus a blend-out of paint or pavement the official data says
is not there. Each pack writes `cache/<ICAO>/corrections.txt`
(feature, imagery vs official, action). See [ACCURACY.md](ACCURACY.md).

## Best free source per airport

| ICAO | Best free public source | Native | Policy |
|---|---|---|---|
| KJFK | NYS ITS 2024 orthos / NYC_Orthos_2024 | 0.15 m | public (`--imagery nys`). After the current hangar fetch validates, queue a near-field pass at `--near-m 0.3` (NYS/NYC 2024 ~15–30 cm) so we beat Raster ~46 cm. Do **not** change the in-flight fetch to 0.3 m. |
| KEWR | NJOGIS 2020 30 cm (COG `s3://njogis-imagery/2020`) | 0.30 m | public; download COGs, then `import-raster` |
| KBOS | MassGIS 2023 15 cm | 0.15 m | public; WMTS / download then `import-raster` |
| KIAD | Loudoun COLOR_2025 30 cm | 0.30 m | public; download then `import-raster` |
| KLAS | Clark County cached imagery | to verify | public; download then `import-raster`. Confirm native metres/px before treating it as sharper than Raster. |
| KLAX | LA County 4-inch layer | 0.10 m | public; download then `import-raster` |
| KMCO | OCPA Aerials2025 | ~0.15 m | public; download then `import-raster` |
| KMIA | Miami-Dade MDCImagery | ~0.15 m | public; download then `import-raster` |
| KSEA | King County KingCo_Aerial_2023 | ~0.08 m | public live pull (`--imagery kingco`) |
| KSFO | San Mateo 2022 ImageServer 15 cm | 0.15 m | public live pull (`--imagery sanmateo`) |
| EDDF | Hessen DOP20 (DL-DE Zero 2.0) | 0.20 m | public; download then `import-raster` |
| EGLL | Environment Agency Vertical Aerial Photography (OGL) | ~0.25 m | public; **Heathrow coverage to verify**, then `import-raster` |
| LEBL | ICGC ortofoto 2024 25 cm / IGN PNOA-MA | 0.25 m | public; download then `import-raster` |
| SEQM | IGM Ecuador orthos WMS 30 cm | 0.30 m | public; download then `import-raster` |
| SKBO | IDECA Bogotá 2025 urban ortho | ~0.15 m | public; **airport coverage to verify**, then `import-raster` |
| KRNO | USDA NAIP (~0.6 m) — only free source | 0.60 m | **Raster fallback** if the local pad is sharper (`import-raster`). Policy `either`. |
| SKRG | *(no verified public source as sharp as Raster)* | — | **Raster fallback** `import-raster` |
| VNKT | Sentinel-2 10 m only (worse than Raster) | 10 m | **Raster fallback** `import-raster` |

Known Raster-fallback airports: **KRNO** (only NAIP is free), **SKRG**,
and **VNKT**.

Licences, fetch commands, and the elevation table stay in
[SOURCES.md](SOURCES.md). The live table in code is
`s376::airport_imagery()`.
