# RSI Raster XT4 parity checklist

The new IG must **beat Raster XT4** on this 737-300. Host packet fields,
visual features, and HAT/query replies stay compatible. **Accuracy is
the top rule** ([ACCURACY.md](ACCURACY.md)): FAA both-end runways,
charted ALS/PAPI, datum/geoid/UTM. Allowed extras are lighter hardware,
a sharper image, and cleaner code — never a wrong threshold to save a
millisecond. The 980 Ti 60 Hz budget is the only other hard constraint.

This table is the source of truth for gaps. Status:

| Status | Meaning |
|---|---|
| **ok** | Decoded and rendered / replied like the original |
| **partial** | Decoded; effect is incomplete or uses our own mapping |
| **gap** | Host sends it (or XT4 showed it); we ignore or stub it |
| **n/a** | Not on the S376 wire / not a Raster feature we must match |

Packet layouts are in [PROTOCOL.md](PROTOCOL.md) (clean-room). Switch
numbers in committed airport TOML are **ours**, not RSI's.

## Host → IG

| Packet / field | Original effect | Status | Notes |
|---|---|---|---|
| 100 eyepoint CSN 1 / minor −1 | Pilot-eye camera, WGS84, heading negated | **ok** | Shared eyepoint; each channel rotates by ViewAngles |
| 100 heading / pitch / roll | Body attitude | **ok** | |
| 100 lat / lon / alt ft | Position | **ok** | |
| 100 other CSN (traffic, marshaller) | Other movers in the scene | **gap** | Counted, body ignored |
| 109 beacon / strobe | Ownship anti-collision flashes | **gap** | Decoded, not drawn |
| 109 landing-light mask + bright | Landing / taxi lobes in the scene | **gap** | Decoded, no beams (NLIM later) |
| 112 switch + step 0–5 | Airport light group intensity | **partial** | GPU points scale by step/5. Switches 1–4 = edge / CL / ALS / threshold (ours). No PAPI, taxi, hold-short yet |
| 113 query type 200 | Ask for HAT/HOT of CSN 1 | **ok** | |
| 124 tod_select −1 / 0…3 | Continuous hour or day/dusk/dawn/night | **ok** | −1 uses `tod_hours`; 0–3 snap to 14.5 / 19 / 6 / 0.5 |
| 124 tod_hours | Sun / sky from local solar time | **ok** | |
| 124 visible_range_ft | Global vis, cap 600 000 ft | **ok** | EXP2 fog density |
| 124 precip type + intensity | Rain / snow / hail in the volume | **partial** | GPU points around the eyepoint (type 1 rain / 2 snow / 3 hail). Density from intensity 0–10. No streaks, no wet-runway look |
| 124 wind speed / dir | Drift, streak, wave cues | **partial** | Precip drifts with wind FROM dir. No waves / smoke |
| 124 lightning trigger | Flash on storm / env | **partial** | Words 10–11 stored. Edge change flashes sun/sky ~4 frames. Storm cells add their own bolts |
| 124 runway condition | Wet / snow surface look | **gap** | Decoded, unused |
| 124 date | Sun season | **ok** | Month/day into `sun_ecef` |
| 105 storm select / active / CSN | Up to 3 of 30 cells on | **ok** | Type 105. CSN 28–30 |
| 105 storm precip / lightning | Rain shaft + bolts in the cell | **partial** | Intensity scales stand-in radius and precip near the cell. lightning 1=random, >1=edge |
| 100 CSN 28–30 | Storm position | **ok** | Lat/lon/alt from type 100 |
| 105 / catalog cell size | Cell diameter | **partial** | Not on the wire (original used IG catalog). 1.5 nm × intensity stand-in |
| 125 / 126 ceiling + top | Lower / upper decks, MSL | **ok** | Cheap stacked discs + 8 cards, GL 3.3. Wind crawls the deck |
| 125 / 126 select | Type / coverage catalog | **partial** | −1 off, ≥0 on. No RSI type table; a layer that is on draws as a solid deck |
| 125 / 126 visAbove | Vis above the deck | **ok** | Caps vis when the eye is above the top |
| 125 / 126 scud / trans | Ragged edges, transition | **partial** | Scud bit 0 widens the disc. Transition heights unused |
| 127 top layer | Highest deck | **partial** | On/off + catalog only. Heights 25–28 kft stand-in (original: IG catalog) |
| 128 fog select / RVR | Fog / RVR overlay | **partial** | RVR caps vis when select ≥ 0. Patchy and speed-cue unused |
| 128 layer height | Fog deck AGL | **partial** | Thickens EXP2 when last HAT is below height+200 ft. No visible deck slab |
| In-cloud fog | Thick vis inside 125/126/127 | **ok** | EXP2 × 2.4 when eye MSL is between ceiling and top |
| 131 display 0/1/2 | Channel off / on / standby | **gap** | Decoded, windows always on |
| Channel geometry | 3 Raster frusta on 4 projector PCs | **ok** | far_left=left −50, mid_left=front 0, mid_right=front 0 (duplicate), far_right=right +50, 50×32.5. Left flip is projector output only. Optional FO `eye_right_m`. |
| Airport lights | Edge, CL, ALS, PAPI, taxi | **partial** | See KJFK/KEWR scenery table. FAA types and sides. Host CSN movers still ignored |
| Terrain / imagery | CERTS + NX tiles | **n/a** | Rebuilt from GLO-30/3DEP + NAIP/S2. Different pixels, same role |
| Airport meshes | NX airport DB | **n/a** | Rebuilt from FAA + OSM + public stand-ins. No RSI geometry |

## KJFK / KEWR 2023 scenery (public rebuild)

What Raster's 2023 KJFK_KEWR pad showed, rebuilt from **FAA / OSM / public**
data only. **FAA/AIP wins** when Raster disagrees (displaced thresholds, ALS
type, PAPI side, length). Tracked in code (`s376::scenery_parity_items`,
`format_official_vs_raster`) and `s376-raster-parity --ours --icao ICAO`.

| Item | Raster 2023 had | Status | How we build it |
|---|---|---|---|
| Runway ends / widths / displaced | NX airport DB | **ok** | FAA both ends. 22R **3424 ft**. 13L **200 ft** |
| Edge / centreline | full | **ok** | FAA AC 150/5340-30 spacing |
| TDZ | charted ends | **ok** | FAA AD 2.14 |
| ALSF-2 + flashers | 04R/22L/13L + EWR 04R/22L | **ok** | Standard 2400 ft bars, not a single row |
| MALSR + RAIL | 31R + EWR 04L/22R | **ok** | |
| RLLS | 13R | **ok** | Lead-in groups |
| PAPI all ends | yes | **ok** | 3.00°; 31R P4R; 13R one P4L aimed 22° left |
| REIL | some ends | **partial** | KEWR 22R/11/29; JFK none on the current FAA chart |
| Blue taxi edge / green CL | full network | **ok** | Schematic + OSM |
| Stop bars | SMGCS | **ok** | Red at taxi starts |
| Lead-on | yes | **partial** | C/U connectors |
| RWSL | yes | **partial** | Hold crossings; not live host-driven |
| Sign glow | yes | **partial** | Yellow points on our signs |
| Taxi names C1–C6, U1–U3 | 2022 diagram; **CC removed** | **partial** | Named connectors; full graph from `s376-cache osm` |
| Signs / hold bars | full | **partial** | At named connectors |
| Terminals incl. T5 | full | **partial** | Schematic T1/T4/T5/T7/T8 + OSM footprints |
| Jetbridges | yes | **partial** | Stand-in boxes at gates |
| Docking guidance | yes | **partial** | Stand-in bars |
| Parked aircraft | static | **partial** | Gate stand-ins. Host CSN still a **gap** |
| Tower + beacon | yes | **partial** | ATCT box + rotating-beacon point |
| ~30 k buildings | yes | **partial** | Official/landmark always. OSM campus 800 + 0.8–16 km city ring 2500 (Queens/Brooklyn/Rockaway). Collision = every footprint (`solids.bin`) |
| Trees | yes | **partial** | Rings off the pavement |
| 12 moving-model routes | yes | **partial** | Paths stored; host CSN movers **gap** |
| Marshallers | yes | **partial** | Gate poses; host CSN ignored |
| Road traffic | yes | **ok** | Instanced cars on OSM motorway/trunk/primary/secondary (class-weighted). Van Wyck, Belt Parkway, JFK terminal roads, NJ Turnpike seeded. TOD density, night lamps, 7 km cull, 280-car 980 Ti batch |
| Summer imagery | CERTS | **partial** | Per-airport public table; Raster-local via `import-raster` |
| Winter imagery | second season | **partial** | `s376-cache fetch --season winter` → `cache/<ICAO>/winter` |
| Road-derived street lights | OSM / public roads | **ok** | Highway-class lamps + building density + apron floods. Never a 60 m lattice; never over water/marsh |
| Full KEWR | yes | **partial** | FAA both ends + scenery stand-ins |
| Classic NYC skyline (easter egg) | n/a | **ok** | Hidden version×7 unlock. Stylized pre-2001 Lower Manhattan (accuracy not required). Draws only in the NYC geofence; stays on for the session; clears on channel reset / refresh_channels |
| Missiles (easter egg) | n/a | **ok** | Same unlock / session. Off until Missiles On. PTT = type 112 switch 255 (or type 109 bit 7 / desk key). Visual only — no host damage. Classic-active: 10 sm keep-out around 40.7115 N, 74.0134 W |
| Operator console | Raster Wizard | **ok** | `s376-wizard` (ImGui + GLFW). Local control channel, not xT4. Channels / Display / Info / Logs. Refresh channels resets session state |

Local compare (never commits Raster data):

```bash
./build/s376-raster-parity --ours --icao KJFK
./build/s376-raster-parity /path/to/local/Raster/KJFK_KEWR --icao KJFK
```

CI fails if any `.nxt` / `.nxb` / `.nx3` / `nxHostPacket.h` enters the tree
(`scripts/check-no-rsi.sh`).

## IG → host

| Packet / field | Original effect | Status | Notes |
|---|---|---|---|
| 200 HAT ft (IEEE BE) | Host radio altimeter / HAT | **ok** | 64-byte host-compatible reply. DEM or building roof (full `solids.bin` set, any tile paging). Off DEM and off every solid: elev=0, hat=alt (stand-in) |
| 200 HOT / terrain elev | Terrain / roof MSL | **ok** | Same as above |
| 200 valid flag | Reply usable | **ok** | Always valid so the host is never starved |

## Image quality (allowed deltas)

These must **not** change what the host put in the scene — only how sharp
or deep it is. Expensive levers are TOML toggles; `render.quality =
"gtx980ti"` (default) or `"rtx"`.

| Lever | Status |
|---|---|
| 4× MSAA (`render.msaa`) | **ok** — GL 3.3 / Maxwell-safe |
| Anisotropic filter (`render.aniso`, default 16) | **ok** — DEM / globe drapes |
| Mipmapped drapes | **ok** |
| FAA AC 150/5340-1 markings | **ok** — geometry (threshold, 5×7 numbers/LRC, aiming, TDZ, CL, edge, hold-short). Stays sharp at grazing angles; no committed textures |
| Asphalt / rubber / grass detail | **ok** — generated 128² atlas (grass/asphalt/dirt/concrete) blended by land cover under the ortho; distance fade (strong <2.2 km). Runway PBR + wet from type-124 `runway_condition`. Optional CC0 Poly Haven / ambientCG via `scripts/fetch-textures.sh` (never committed) |
| OSM buildings | **partial** — extruded official footprints (unchanged) + facade/roof palette by OSM `building=*` / schematic role, night window emissive. Mesh: official always, OSM campus 800, city ring 2500 (0.8–16 km). Collision = every footprint (`solids.bin`) |
| Analytic sky + aerial perspective | **ok** — Rayleigh/Mie sky (Nishita-lite, GLSL 120), visual sun from host type-124 TOD buckets (dusk is dusk at any airport/date; `render.solar_astronomy` keeps `sun_ecef`), aerial haze from host visibility, dawn/dusk/night. Toggle `render.atmosphere`. Not a full Bruneton LUT |
| Water / coastlines | **ok** — Jamaica Bay / ocean from DEM `land_coverage` vertex mask (1 m olive ortho no longer kills the shader) + chroma: sky reflection, Fresnel, sun glint, wave scroll, foam. Toggle `render.water` |
| Oblique mip / aniso | **ok** — BC1 mip chain generated on load if the DDS has none; trilinear + aniso 16 (floor 8). lod0 near the eye, lod1/lod2 reserved to 40 nm |
| Horizon seam / haze | **partial** — altitude `uHaze` + grazing fade; sky mixes to fog at the horizon. Not a Bruneton LUT |
| Cloud soft edge / lighting | **partial** — hash edge + sun wrap on the weather-round decks |
| HDR + ACES | **ok** — ACES in every forward shader; optional filmic lift/gamma/gain (`render.filmic`); `render.exposure` |
| Bloom / night glow | **ok** — Raster-style fog halo on point lights (`render.bloom`) + city-glow emissive on terrain at night |
| FXAA | **partial** — window POST_RENDER pass (`render.fxaa`); screenshot/record FBO stays MSAA-off |
| SSAO | **partial** — cheap wall darkening; on for both `gtx980ti` (one channel) and `rtx`. No depth post-pass |
| Sun shadows | **ok** — one-cascade 1024 sun CSM when `render.shadows` (on `rtx`, off `gtx980ti`) |
| GPU point sprites for lights | **ok** — GLSL 120 points (no PointSprite / POINT_SMOOTH); still step 0–5 |
| GL compatibility (not Core) | **ok** — Linux leaves the context version unset (NVIDIA 4.6 compat). Elsewhere 3.3 + `COMPATIBILITY_PROFILE_BIT`. Scene lighting/fog/points are shaders so a CORE context does not emit `invalid enumerant` |
| `--screenshot PATH --at ICAO[:RWY[:nm]]` | **ok** — offscreen RGBA8 FBO at the channel's WxH (not the decorated window), `GL_PACK_ALIGNMENT` 1, vertical flip, PPM+PNG via stb. Warm-up until pager idle or `--screenshot-wait` (default 3 s). `--at` does not open a host socket |
| `--record PATH --approach ICAO:RWY` | **ok** — scripted straight 3° (or `--gs`) glidepath from `--start-agl` to the landing threshold. Cars animate at `--fps`. One RGB frame in RAM; ffmpeg rawvideo pipe when on PATH, else PNG sequence + printed mux command. Same FBO + pager + `ram_mb`/`vram_mb` cap as `--screenshot` |

## Channel / desk

| Item | Status |
|---|---|
| 4 projector PCs, Raster 3 channels | **ok** — captain: far_left+mid_left; FO: mid_right+far_right; both mids = front. `$S376_CHANNEL` |
| Host-stream frame sync | **ok** — `render.sync_to_host` draws only on a new type-100 eyepoint |
| HAT reply master | **ok** — `host.reply_master` / `--reply-master` (default mid_left); other PCs listen only |
| Per-seat eyepoint | **partial** — `eye_right_m` / `eye_fwd_m` / `eye_up_m` optional on FO channels; default 0 (shared host eye) |
| Laptop 3-window | **ok** — `config/channels-3.toml` (front/left/right names) |
| 4-monitor desk pin (`config/desk-4monitor.toml`) | **ok** — one-PC hangar desk: IOS on DP-4, L/F/R on DP-3 / HDMI-0 / DP-0 |
| IOS / debug view | **partial** — same eyepoint, pitched −18°, 60° FOV. Not a second ownship |

## Safety (must never regress)

- No RSI / CAE files, ICD text, captures, or NX tiles in git (`scripts/check-no-rsi.sh`)
- Tests/CI never send to `10.1.95.0/24`, `192.168.173.0/24`, or UDP 3340/3341
- `s376-raster-parity` reads a local Raster path at runtime only; it does not copy or embed vendor files
