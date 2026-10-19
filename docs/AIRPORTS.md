# Airport rebuild plan

The RSI airport databases (about 17 GiB of `.nxb` / `.nxt`) cannot be
converted. s376-visual rebuilds each field from open data plus published
lighting standards. The owner's RSI `.nx3` text files (runway thresholds,
headings, light-switch maps) are a **private local reference only** and
must never be committed.

## Fields we need (from the live XT4 install)

| ICAO | Name | Priority |
|---|---|---|
| KLAS | Harry Reid / McCarran, Las Vegas | 1 — typical start |
| KSFO | San Francisco Intl | 1 |
| KSEA | Seattle-Tacoma | 1 |
| KLAX | Los Angeles Intl | 1 |
| KJFK | New York Kennedy | 1 — FAA both ends + accuracy suite |
| KEWR | Newark Liberty | 1 — FAA both ends + accuracy suite |
| KBOS | Boston Logan | 2 |
| KIAD | Washington Dulles | 2 |
| KMCO | Orlando Intl (+ KORL) | 2 |
| KMIA | Miami Intl | 2 |
| KRNO | Reno-Tahoe | 2 |
| EGLL | London Heathrow | 2 |
| EDDF | Frankfurt Main | 2 |
| LEBL | Barcelona El Prat | 3 |
| SEQM | Quito Mariscal Sucre | 3 |
| SKBO | Bogotá El Dorado | 3 |
| SKRG | Rionegro / Medellín | 3 |
| VNKT | Kathmandu Tribhuvan | 3 |

Plus two generic “anywhere” pads later, for host airport-select fallback.

## Data sources (all public)

| Layer | Source | What we take |
|---|---|---|
| Airport / runway / displaced threshold | Official FAA NASR / national AIP AD 2. OurAirports is a **cross-check only** | ICAO, lat/lon, elev, runway ends, width, ALS, PAPI, REIL, TDZ |
| US runway + NASR | [FAA 28-day NASR / Chart Supplement](https://www.faa.gov/air_traffic/flight_info/aeronav/digital_products/) | Authoritative US thresholds, TCH, declared distances, lighting |
| Non-US AIP AD 2 | DFS Germany, NATS UK, ENAIRE Spain, DGAC Ecuador, Aerocivil Colombia, CAAN Nepal | EDDF / EGLL / LEBL / SEQM / SKBO / SKRG / VNKT |
| Taxiways, aprons, buildings, parking | [OpenStreetMap](https://www.openstreetmap.org/) via Overpass (`aeroway=*`) | Polygons and lines. ODbL attribution |
| Terminal / hangar massing | OSM `building` + a later simple extrude. Optional public CityGML | Boxy is fine for v1 |
| Imagery under the airport | Best free public source per ICAO, else Raster-local — see [IMAGERY_SOURCES.md](IMAGERY_SOURCES.md) | Draped, not modelled. Official geometry wins over the photo |
| Elevation under the airport | Copernicus GLO-30, then a local airport flatten | Conform runways to a plane |

## Lighting — from standards, not from RSI binaries

Light **positions** come from runway geometry plus the published
standard for that approach. Light **switch numbers** (which host
packet 112 turns up) will be assigned in our own airport TOML so they
can be lined up with the host later. The private `.nx3` switch map is
the cheat-sheet for that lining-up step; the committed files use our
names.

| System | Standard | Layout rule (short) |
|---|---|---|
| Runway edge | FAA AC 150/5340-30, ICAO Annex 14 | White (yellow last 600 m / 2 000 ft on instrument runways), 60 m / 200 ft spacing, two rows at runway width |
| Runway centreline | same | 15 m / 50 ft; red/white then red in the last 600 m |
| Threshold / end | same | Green threshold bar, red end bar |
| Touchdown zone | same | White bars 150–900 m from threshold on precision runways |
| PAPI / VASI | AC 150/5345-28, ICAO | Four (or two) boxes, 15 m left of edge, ~300 m from threshold, 3° glide typical |
| ALSF-2 | FAA approach-light spec | 2 400 ft centreline bars + 5-bar crossbar at 1 000 ft + side rows. KJFK / KLAX / KSFO precision ends |
| ALSF-1 / MALSR / SSALR / ODALS / REIL | same family | Shorter / fewer bars. Match the charted system per end |
| Taxi edge / centreline | AC 150/5340-30 | Blue edge, green centreline. OSM taxiway graph is the path |
| Hold-short / stop bar | SMGCS / ICAO | Red stop bar, yellow hold |
| Approach REIL / wig-wag | same | Paired white flashers at threshold |

Intensity steps are **0–5** (packet 112). The GPU light-point pass
scales colour and alpha by `step / 5`. Switch numbers are ours:

| Switch | Group |
|---|---|
| 1 | Runway edge |
| 2 | Runway centreline |
| 3 | Approach (ALSF-2 / MALSR bars) |
| 4 | Threshold / end |

Every public airport is built from **both official ends**
(`airport_from_faa` / `faa_all()`). ALS/PAPI/REIL/TDZ/EMAS match the
current FAA Chart Supplement (US) or national AIP AD 2 (non-US). The
accuracy suite fails CI if an end, heading, threshold, ALS type, PAPI
side/angle, REIL, TDZ, or width drifts. Official data wins over
older charted ALSF-2 guesses (KLAS 26R **MALS**, 26L **MALSF**;
KLAX 24L **MALSR**; KBOS 15R **MALSR**, 33L **ALSF-2**). See
[ACCURACY.md](ACCURACY.md) for Raster differences (22R displaced
**3424 ft**, 13L **200 ft**, 04L/22R ALS **NONE**, 13R **RLLS**).

Edge / centreline / approach / threshold points use the spacing rules
above. Painted markings follow FAA AC 150/5340-1. Taxi and apron
polygons start as schematic offsets; `s376-cache osm ICAO` replaces
them with Overpass `aeroway=*` + `building=*` (ODbL). Runtime loads
`cache/<icao>/osm.bin` (mesh) and `solids.bin` (every footprint for
HAT). Height is OSM `height`, else `building:levels` × 3.5 m, else
12 m, on DEM ground.

`data/airports/{ksea,ksfo,klas,klax,kjfk,kewr,kbos}.toml` documents the
same public numbers. Do not paste RSI `.nx3` coordinates into them.

Side-by-side vs Joey's Raster 4R photo (reference only, never commit):

```bash
./build/s376-visual --screenshot /tmp/jfk-04r.ppm --at KJFK:04R:0 --channel mid_left
./build/s376-visual --screenshot /tmp/bos-04r.ppm --at KBOS:04R:0 --channel mid_left
./build/s376-visual --screenshot /tmp/jfk-04r-3nm.ppm --at KJFK:04R:3 --channel mid_left
./build/s376-visual --screenshot /tmp/jfk-bank.ppm --at KJFK:04R:8 --alt 3000 --bank 30 --channel mid_left
./build/s376-visual --screenshot /tmp/jfk-12k.ppm --at KJFK --alt 12000 --hdg 036 --channel mid_left

# Joey video: straight 3° to 04R touchdown, cars moving. Mint RTX 5060 Ti:
./build/s376-visual --cache-dir cache/KJFK --channel mid_left --approach KJFK:04R \
  --start-agl 2000 --gs 3.0 --kts 140 --fps 30 --record out.mp4
# if ffmpeg is missing: PNG dump + printed mux
# ffmpeg -y -framerate 30 -i out-frames/frame-%06d.png -c:v libx264 -pix_fmt yuv420p out.mp4
```

## Suggested build order

1. **KSEA 16L/34R** — synthetic takeoff from that threshold in
   `s376-synth`. In the tree.
2. **KSFO** — complex ALS, water, hills. In the tree.
3. **KLAS** — desert, lots of night light points. In the tree.
4. **KLAX** then **KJFK** — parallel-runway ALSF-2. In the tree.
5. **KBOS** — 04R is the other candidate for the Raster daytime photo. In the tree.
6. **EGLL / EDDF** — ICAO lighting, Sentinel-2.
7. The rest in the table order.

## What stays private

On the owner's PC, `domain.nx3` and friends list RSI switch numbers,
threshold lat/lon, and light-group names. Use them to **audit** our
layouts and to pick switch numbers the host already sends. Do not copy
the files into this public repo, do not paste blocks of them into
commits, and do not check in screenshots that are just the RSI GUI.
