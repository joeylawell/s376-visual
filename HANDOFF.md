# HANDOFF

A coworker (human or agent) can pick up from here. Cursor usage on the
original account may stop mid-run — **this file is the source of truth**.
Update it at the end of every run.

**PR #1 (visual v1):** https://github.com/joeylawell/s376-visual/pull/1 (draft)
**Branch:** `cursor/s376-visual-v1-6852` (quality + defect fixes merged here)
**PR #4:** https://github.com/joeylawell/s376-visual/pull/4 (quality-gtx980ti; merged into #1)
**Repo:** https://github.com/joeylawell/s376-visual (public)

**Top rule:** beat Raster in every way; **accuracy first**. FAA is the
truth when it disagrees with Raster. Never trade a surveyed number for
looks, speed, or disk. **Disk is not a constraint** — the pack may
exceed Raster's 857 GB CERTS farm; the owner will fit new SSDs in the
visual PCs. Quality and accuracy win over storage. The remaining hard
constraint is the GTX 980 Ti **runtime** RAM/VRAM / 60 Hz channel
budget (`ram_mb=2048`, `vram_mb=5120`). **Exception:** the Classic
NYC skyline easter egg is art-directed (see `docs/ACCURACY.md`).
See also `docs/QUALITY.md` and `docs/PARITY.md`.

Never commit RSI / CAE files, ICD text, NX tiles, Raster photos, or
`nxHostPacket.h`. Never contact `10.1.95.0/24` or `192.168.173.0/24`
from tests/CI. `scripts/check-no-rsi.sh` fails the build if
`.nxt` / `.nxb` / `.nx3` / `nxHostPacket.h` enter the tree.

---

## Current state (2026-10-19, OSM / eye / critique)

Feature branch **`cursor/kjfk-osm-eye-6852`** (base `cursor/s376-visual-v1-6852`).
Do not wait on Mint. Public data lives in **`/tmp/kjfk-real`** (never commit).
3DEP/NYS `s376-cache fetch` is still paging lod0 (leave that PID alone).

### What shipped this increment

1. **OSM metro bake no longer evicts Manhattan.** Working set
   `kMaxOsmBuildingsBake=80000`, rank in 4k batches, then `write_osm_bin`
   keeps 12 k. Rebaked from local Overpass tiles (reuse, no network):
   **12 000 buildings, max 472 m (Central Park Tower), 5 586 ≥ 40 m,
   Empire State 443.2 m.** osm.bin is **v3** (type survives).
2. **`shoreline.bin`** from OSM `natural=water` (64 rings). Loaded
   **before** globe/tiles. 04R short-final stays water even when the
   open inlet is not a closed OSM pond. Rockaway is the thin corridor.
3. **KJFK terminals T1/T4/T5/T7/T8, hangars, ATCT** from OSM (T4 462-pt
   / 29.7 m, T5 131-pt / 13.6 m Saarinen, T1 134-pt). Light NYS-style
   roofs. Jetbridge query added (`aeroway=jet_bridge`). Night: roof
   floods on Airport buildings + stronger facade windows.
4. **737-300 eye + 4-channel 50°×32.5° strip** already on this branch
   (`--channel strip`, `--at KJFK:04R:6`).
5. **Day ALS** stays aviation-white, noon intensity **0.18** (PAR-56
   bar, not a cyan bay wash). Night 0.6 nm ALSF-2 + PAPI read.
6. **Three critique rounds** of `--at` day/dusk/night/wet vs public
   refs (viewed, not committed). Worst 10 each round; remaining:
   globe-hex water at 0.6 nm (lod0 1 m still paging), tan far-field
   until NYS tiles land, terminals still shy at 3 nm, Manhattan
   landmarks still dominate the OSM ridge.

**Tests:** `s376-tests` 131841/0, `s376-fakehost-tests` 44835/0.
**Budget:** `gpu_budget_ms=12.2` unchanged (980 Ti). Skyline mesh
800 (was 400) — still under the 1.00 ms buildings line.

**Frames (not in git):**
`/opt/cursor/artifacts/kjfk_04r_0.6nm_r1_before.png` →
`kjfk_04r_0.6nm_day_r3.png`; same for 3 nm and Manhattan.
Night: `kjfk_04r_0.6nm_night_r3.png`,
`kjfk_manhattan_2500_night_r3.png`.

```
s376-cache osm KJFK --city --out /tmp/kjfk-real   # reuses osm-tiles/
s376-visual --offscreen --quality gtx980ti --cache-dir /tmp/kjfk-real \
  --at KJFK:04R:0.6 --tod 12 --channel mid_left --screenshot /tmp/out.ppm
```

---

## Previous increment (2026-10-19, PR #1)

Work stays on **PR #1** (`cursor/s376-visual-v1-6852`). Accuracy-first.
**980 Ti is the target.** No RSI / imagery / Raster files in git.
Do not add `.deb`, system-wide systemd units, or an `rtx-xxxxx`
profile. `scripts/hangar-queue.sh --install-autostart` writes a
**user** unit under `~/.config/systemd/user/` (no sudo) so Joey's
16 GB Mint can resume after a RAM freeze/reboot.

**BingBongBot `s376-fakehost` is merged.** Loopback-only
(`fakehost::guard()` — env vars cannot relax it). Speaks the existing
xT4 subset plus type **180** ownship gear/flaps (our design, not
CAE ICD). `--demo kjfk` now drives `fakehost::run_approach_sender`
on 127.0.0.1:2300 / 2301 (six-mile-final, 6 nm default; `--start
6sm`). `--record --demo kjfk` stays in-process. Joey's Mint IOS /
host emulator maps onto `HostState` / `ControlSource` later — keep
those adapters flexible.

Windows MSVC CI (980 Ti zip) was red since the zip job: `windows.h`
`min`/`max` macros, MSVC `__BYTE_ORDER__` both-zero `#error`, and
`path::native()` (`wstring`) `.find("..")`. Fixed with `NOMINMAX`,
a portable LE check, and `generic_string()`. `s376-fakehost` exe +
tests are UNIX-gated so MSVC still builds `s376_core` / `s376-visual`.

**Joey priority: Oct 25 KJFK demo.** Hard deadline Sunday 2026-10-25
for a 2-minute 04R approach + landing (day and night), believable
lighting and haze, Manhattan skyline, water, terminals, runway and
approach lights, locked 60 fps, no black slabs / holes / sky bands /
pop-in. KEWR / other airports stay behind `--all-airports`.

### Host interface (merged)

Documented in [docs/HOST_INTERFACE.md](docs/HOST_INTERFACE.md) and
[docs/FAKEHOST.md](docs/FAKEHOST.md). Type **180** is our ownship
packet (gear 0/1/2, flaps 0/1/2/5/10/15/25/30/40) — not ICD text.

| Direction | Default | Config | Code |
|---|---|---|---|
| Host → IG | UDP **`127.0.0.1:2300`** (listen `0.0.0.0:2300`) | `host.bind` | `HostConfig` in `include/s376/config.hpp` |
| IG → host HAT | UDP **`2301`** to the query sender | `host.reply_port` / **`--reply-port`** | `HostSocket::send_hat` |
| Operator console | UDP **`127.0.0.1:2376`** | `[control].bind` | **not** xT4 |

Input snapshot: `s376::HostState` (`include/s376/state.hpp`) —
`eye` (type **100**), `env` (**124**), `fog` (**128**), clouds
(**125–127**), storms (**105**), `lights` (**109**),
`light_steps` (**112**), `ownship` (**180**), `display` (**131**),
`script_dt` (in-process). Wire structs + builders:
`include/s376/xt4.hpp` (`build_ownship` / `decode_ownship`).
Receiver: `HostReceiver` / `HostSocket` (`include/s376/host.hpp`,
`src/host.cpp`). Safety: `include/s376/safety.hpp` refuses
`10.1.95.0/24`, `192.168.173.0/24`, UDP **3340 / 3341** unless
`allow_sim_lan`. `fakehost::guard()` is stricter (loopback only).

HAT + collision: host type **113** query whose first i32 is **200**;
IG replies type **200** (`xt4::build_hat_response`). Same packet is
the collision reply (`HAT ≤ 0`). Sample: `HatSample`
(`include/s376/terrain.hpp`); solids:
`sample_hat_solids` / `sample_session_hat`
(`include/s376/landmarks.hpp`). Official KJFK T5 + optional
`/tmp/kjfk-real/solids.bin` / `cache/KJFK/solids.bin` contact is
tested. Multi-channel: only `reply_master` sends type 200.

`--demo kjfk` = fakehost loopback (six-mile-final, 6 nm). Bare
`--demo` is CI synth to `127.0.0.1:2300`.

### Oct 25 backwards plan (daily demoable build)

| Day | Ship |
|---|---|
| **Mon 19** (today) | PR #1: fakehost merged, type 180, `--demo kjfk` via loopback, `--reply-port`, reply_master-only HAT, hangar-queue crash resume. |
| **Tue 20** | Mint online: `hangar-queue.sh` 01–08 (build, review, bench, day/night video, far/near fetch). |
| **Wed 21** | Real-pack critique vs JFK 04R photos: water, runway, terminals, horizon. Fix defects. |
| **Thu 22** | Night lighting + Manhattan skyline + Jamaica Bay + terminals. |
| **Fri 23** | Lock 60 fps on 5060 Ti (autodrop if over 12.20). Record the 2-minute approach. |
| **Sat 24** | Dress rehearsal day + night. Bug squash only. |
| **Sun 25** | Boss demo: KJFK 04R, ~2 minutes, day then night. |

### Also in this increment

1. **`--demo kjfk` via fakehost.** Loopback UDP 2300/2301, six-mile
   final, 6 nm default (`--start 6sm` / `--six-mile-final`).
   `--tod 12` day, `--tod 3` night. `--quality gtx980ti`,
   `--channel mid_left`. `--record` stays in-process
   `ApproachProfile`. Bare `--demo` stays the CI synth path.
   `--reply-port PORT` overrides `host.reply_port`. Type **180**
   carries gear/flaps every frame.
2. **Live 980 Ti path.** TAA (MSAA 0), 2-cascade sun CSM 512+512
   (±400 / ±2200 m), 32-light clustered ground lighting, HDR ACES +
   bloom + SSAO + atmosphere, page-in lod fade (lod1/lod2 start at 1;
   stills snap). Autodrop order: SSAO → shadows → bloom → FXAA → TAA
   → lod_fade → detail → filmic → clustered. Never drops water /
   markings / buildings / atmosphere. Budgeted total **12.20 ms**
   with shadows (11.35 without).
3. **`scripts/hangar-queue.sh` redline + crash resilience.** Default
   steps **01–12 are KJFK only**. Every step `MemoryMax=12G`. Default
   **`-j 16`**. `--install-autostart` writes a user systemd unit
   (no sudo) that resumes after reboot. Heartbeat + `boot_id` detect
   an unclean stop (leftover `current` or a different `boot_id`) and
   increment that step’s crash counter; journal OOM/Xid/thermal;
   step down (`-j` 16→8→4, rtx→gtx980ti, serial fetches, heat 83→75)
   and **BLOCKED** after 3. PSI/MemAvailable watchdog keeps ~4 GB
   free and stops only this queue's units. `CRASHES.md`.
   `--mem-budget` (default 2048) is passed to `s376-visual` and
   `s376-cache`. KEWR / US-next are **13/14 behind `--all-airports`**.
4. **OSM pack isolation.** `try_load_osm` only loads `osm.bin` when
   `coverage.toml` / `region.toml` / folder / unique path ICAO
   matches the airport. `/tmp/kjfk-real` and `cache/KJFK-near` no
   longer dump 66 k lights into KSEA.
5. **Real KJFK pack on this VM (not committed).**
   `s376-cache fetch KJFK --out /tmp/kjfk-real` — lod0 1 m NYS + 3DEP,
   lod1/lod2 to 40 nm / 74 km, OSM. Resume-safe. Never commit imagery.
6. **`--bench` paste line.**
   `SUMMARY  s376 --bench quality=… budget=… max=…ms@…nm avg=… dropped=… result=UNDER|OVER|NO_GL 60Hz=YES|NO|SKIP`.
7. **`--offscreen`.** Pbuffer first; parked fallback window at
   −32000,−32000.
8. **`scripts/hangar-review.sh`.** 04R shot list + Manhattan hdg 310
   day/night + `--bench` into `~/Downloads/s376-review-<date>/`.

**Joey, when Mint is back:**
```
cd ~/s376-visual && git pull && scripts/hangar-queue.sh
# KJFK demo only. Other airports: scripts/hangar-queue.sh --all-airports
# Override workers: scripts/hangar-queue.sh -j 8
```

**980 Ti GPU:** live path is TAA + cheap CSM + clustered. Total stays
**12.20 ms** shadows-on. Joey runs `--bench` and `--demo kjfk` on the
5060 Ti, later the 980 Ti.

**Previous increment (still on PR #1):** Hangar far-field / Windows
zip / `--bench` table / region plans. Synth review frames were
rejected (flat colors / sand triangle) — do not use them for looks.

**Previous increment (still on PR #1):** Hangar far-field (empty
sky / lod1/lod2 / globe / water mask). Joey: “looks like gaza.”
Five defects from `refs/real-front-299nm-tod12.png` and
`real-front-06nm-tod12.png`. Hangar 1 m pack still not on this box —
public-source `write_kjfk_farfield_subset` matches Joey’s layout.

1. **Far field is no longer empty sky.** Packed tiles used to skip the
   globe and then **evict any lod1/lod2 that overlapped lod0**. A 50 km
   lod2 covering the airport disappeared the moment 1 m tiles paged in,
   so Jamaica Bay / Rockaway / Atlantic / Queens were sky-blue. Globe
   now always sits 80 m under the tiles. Pager **reserves** lod1/lod2
   out to **40 nm (74 km)** and only evicts a coarse tile when lod0
   *fully* covers it. Fetch default outer radius is 74 km. Regression:
   `far_field_is_empty_sky` + `write_kjfk_farfield_subset` + optional
   GL 2.99 nm shot (`DISPLAY` set).
2. **Sky band (~10% from the top).** 1920×1200 channel on a 1080p Mint
   window: `glReadPixels` of 1200 rows past a 1080 drawable. Viewport
   follows the drawable; screenshots prefer the channel-size FBO.
   Sky shader is a smooth zenith→horizon mix (`gl_FragCoord` NDC).
3. **Floating black slivers** left of the airport were hangar/jetbridge
   cards silhouetted against the missing far ground, plus distant walls
   drawn as hairline planes. Far terrain + aerial lift on mat 3/4 past
   2.8 km.
4. **Near-field smear / no Queens buildings.** BC1 mip chain is
   generated when the DDS has none (aniso 16, floor 8). lod0 still
   wins near the eye. OSM mesh is no longer “nearest 2500 to ARP”
   (that filled with airfield sheds): official always, campus 800,
   0.8–16 km city ring 2500. `kMaxOsmBuildings` 12 k.
5. **0.6 nm olive water slab.** `make_dem_patch` water flag is now
   `vWaterMask` (texcoord1.z). 1 m NYS bay is olive so chroma-only
   wet never fired; the cover mask drives the water shader.

**980 Ti GPU:** globe **+0.05** absorbed in the 6.00 ms base. Total
stays **12.20 ms** shadows-off.

Before (Joey hangar): `refs/real-front-299nm-tod12.png`,
`refs/real-front-06nm-tod12.png`. After: recapture on this box with
the public subset (`/tmp/kjfk_04r_2.99nm_farfield_after.png`,
`/tmp/kjfk_04r_0.6nm_farfield_after.png`).

**Previous increment (still on PR #1):** Night streets / 04R HIRL from
`kjfk_04r_0.6nm_night.png` / `_dusk.png`. Official runway/taxi
coordinates unchanged. Hangar 1 m pack still not on this box
(`/tmp/kjfk-tiny` ~8 m). Mint on a phone hotspot — do not wait.

1. **Fake yellow lattice gone.** `add_scenery_lights` no longer
   stamps a 60 m ENU grid around the ARP (that painted marsh/bay
   south of JFK). City/street lamps now sample `a.roads` (public NY
   + OSM highway class) at cobra-head spacing, plus OSM building
   density and official/OSM apron floods. `land_coverage` < 0.50
   skips water/marsh. Cap 800 Night points (980 Ti).
2. **04R HIRL + CL + TDZ read.** CAT III 04R already had FAA edge
   (60 m), CL (15 m), TDZ (30 bars / 900 m). Lattice drowned them
   and the asphalt was a black wedge. Night size boost + range
   floor + 0.85 m fixture height + `kLitColorFS` HIRL sheen.
3. **Green line left of 04R is taxi B centreline.** Taxi B is
   `parallel_taxi("B", 04R, −80 m)` on the 04L side. Taxi colours
   were inverted (edge green / CL amber). Now FAA: edge aviation
   **blue**, CL aviation **green**.

**980 Ti GPU:** lattice ~2200 pts → ~400 road lamps **−0.15 ms**;
asphalt sheen **+0.05 ms** in existing markings pass. Net **−0.10**.
Budgeted total stays **12.20 ms** shadows-off.

**CI linux xvfb:** 7516bea failed `no OpenGL error spam` —
`invalid enumerant` at `Before Renderer::compile` on windowed
`--demo`. Headless was clean. Cause: GitHub Mesa has no
`LIBGL_ALWAYS_SOFTWARE` / `GALLIUM_DRIVER`, so the quality-pass FXAA
FBO (`GL_DEPTH_COMPONENT24`) ran on a context that can be CORE.
No `/dev/nvidiactl` now takes the software/compat path (hangar 980 Ti
keeps NVIDIA 4.6 compat). FXAA depth uses `GL_DEPTH_COMPONENT`.

Before (reviewed lattice): `kjfk_04r_0.6nm_night.png`,
`kjfk_04r_0.6nm_dusk.png`. After: `kjfk_04r_0.6nm_night_after.png`,
`kjfk_04r_0.6nm_dusk_after.png`.

**Previous quality defect pass (merged):** visual TOD, dusk `--tod
19`, Jamaica Bay 8 m slab, left-horizon 04L edge (no pier mesh).

**Quality pass (gtx980ti budget):** GTA V-level levers, each toggleable
and budgeted for one 980 Ti channel at 1920×1200 / 60 Hz. Official
geometry and lights are unchanged. Hangar Mint PC is offline — real
980 Ti `GL_TIME_ELAPSED` waits. This box is llvmpipe.

| Feature | Toggle | 980 Ti ms |
|---|---|---|
| base | — | 6.00 |
| Rayleigh/Mie sky + aerial | `render.atmosphere` | 0.45 |
| Water (Fresnel/glint/waves/foam) | `render.water` | 0.80 |
| Land-cover detail under ortho | `render.detail_textures` | 0.55 |
| OSM facades + night windows | `render.buildings` | 1.10 |
| Runway PBR + wet (type 124) | `render.markings` | 0.30 |
| Light halo + city glow | `render.bloom` | 0.70 |
| Filmic after ACES | `render.filmic` | 0.15 |
| FXAA (window) | `render.fxaa` | 0.35 |
| MSAA 4x (window) | `render.msaa` | 1.80 |
| One-cascade sun CSM | `render.shadows` | 1.80 (off on gtx980ti) |

Total **12.20 ms** shadows-off / **14.00 ms** shadows-on (under 16.67).
`--tod HOURS` and `--rwy-cond N` pose the host env for shots.
See `docs/QUALITY.md`.

llvmpipe frames (`/tmp/kjfk-tiny`, hangar 980 Ti not on this box):

- before: `/opt/cursor/artifacts/kjfk_04r_0.6nm_quality_before.png`,
  `kjfk_04r_2.99nm_quality_before.png`
- after day: `kjfk_04r_0.6nm_quality_after.png`,
  `kjfk_04r_2.99nm_quality_after.png`
- dusk `--tod 19`: `kjfk_04r_0.6nm_dusk.png` (before: `…_dusk_before.png`)
- night `--tod 3`: `kjfk_04r_0.6nm_night.png` (before: `…_night_before.png`)
- wet `--rwy-cond 2`: `kjfk_04r_0.15nm_wet.png` vs `…_dry.png`
- toggles: `kjfk_04r_0.6nm_atmo_off.png`, `…_water_off.png`, `…_detail_off.png`

Tests: **130633** pass. Real 980 Ti GPU ms waits on Joey's Mint PC.

**Light size / PAPI / tile seams (prior increment):** The ~13 large
white/red/green hemispheres on the 0.6 nm horizon were 22L ALSF-2 /
threshold / end (and taxi) drawn at the near-field HAT size (~20 px
circular point sprites). `point_size_px(tod, hat, range)` now applies
the HAT boost only inside `kSizeNearM` (350 m); distant lights clamp
to the Raster 2.5–2.8 px floor and never grow with range. The point
shader sets per-vertex `gl_PointSize` from eye distance, fades with
inverse-square + fog (alpha, not fog-coloured discs), and uses a
soft core. A debug test projects 04R PAPI into the 0.6 nm front
frame and asserts four distinct in-frame lights, left of the
runway, coloured 2W/2R, with pixel gap > 1.6× point size; the same
test asserts the 22L horizon row is < 3.2 px. Tile-edge grid over
the bay: UV is `x/(w-1)` (shared edge texels), water uses geodetic-up
and flattened z, land uses DEM slope normals, and detail/wave noise
is keyed from lon/lat so it is continuous across tiles.

**ALS / sky / water (prior increment):** 04R ALSF-2 is on the extended
centerline (2400 ft bars, 1000 ft crossbar, red side-row barrettes,
SFL) on DEM or over-water piers; daytime approach intensity is
Raster-readable. PAPI is four boxes left of 04R at 3.00° and shows
2W2R on a 3° path. The far-end black blob was a full-length 0.10
rubber strip (now 900 m/end at 0.26). Sky fills the VFOV (undecorated
1920×1200, oversized NDC quad). Near-field water is Jamaica Bay:
lod0 + skirts + flattened ocean verts + a south-of-JFK land_coverage
hole. Tiny pack is still ~8 m.

**Doctor AIP labels / stale SKBO+VNKT:** non-US `faa-*` checks print
`vs DFS|NATS|ENAIRE|DGAC Ecuador|Aerocivil|CAAN <date>`, not
`vs FAA`. SKBO 2016 and VNKT 2024 are flagged `aip-stale-*` (warn)
because the current Aerocivil AIRAC AMDT 73/26 and CAAN eAIP AD 2.12
tables were not fetchable. Geometry is unchanged.

**Channel geometry (this increment):** every screenshot/record uses
Raster 50° × 32.5° and the per-channel heading offsets
(+35.515 / 0 / −35.25), including flipped Left. Default output is
**1920×1200** (tan(25°)/tan(16.25°) ≈ 1.60, 16:10) until Joey
confirms the projector resolution. Per-channel `resolution = "WxH"`
on `[[channels]]` and the s376-wizard Display tab. `--channel all
--record` / `--screenshot` writes a stitched Left|Front|Right strip
(5760×1200). Verification frame uses this geometry.

llvmpipe KJFK:04R:0.6 and 2.99 / 962 ft (`/tmp/kjfk-tiny`):
`/opt/cursor/artifacts/kjfk_04r_0.6nm_points_verified.png` and
`/opt/cursor/artifacts/kjfk_04r_2.99nm_962ft_points_verified.png`.
Horizon 22L ALS/taxi are ~2.5 px points (no 20 px hemispheres).
04R PAPI is four distinct 2W2R lights left of touchdown. Tests:
130589 pass.

**Hangar record frame 140 (dda4f06) fixes:** clip-space sky camera
(no zenith trapezoid, no hash grain); DXT pad to mod-4/POT at pack
**and** load (2011×2033 → 2048); no mipmap-gen on compressed
level-0 (stops `invalid enumerant` after compileGLObjects);
llvmpipe decodes DXT→RGB so ortho binds; incomplete tiles
skipped once; atomic `elev.s376dem` / `imagery.dds`; persistent
record FBO + window readback fallback; MSAA off for record/shot;
DO_NOT_COMPUTE_NEAR_FAR; airport mesh polygon-offset.

**Scripted approach video** (`--record` / `--approach`):
offscreen FBO, straight geometric glidepath to the landing
threshold, cars step at `1/fps`. One RGB frame in RAM. ffmpeg
rawvideo pipe when on PATH, else PNG sequence + printed mux.

```bash
./build/s376-visual --cache-dir cache/KJFK --channel mid_left --approach KJFK:04R \
  --start-agl 2000 --gs 3.0 --kts 140 --fps 30 --record out.mp4
# fallback if no ffmpeg:
# ffmpeg -y -framerate 30 -i out-frames/frame-%06d.png -c:v libx264 -pix_fmt yuv420p out.mp4
```

**Airport structures are exact-solid** (same box as the mesh):
KJFK T1/T4/T5/T7/T8 + hangar + cargo + ATCT + jetbridges; KEWR
A/B/C + hangar + tower + jetbridges. OSM houses stay on the cheap
`solids.bin` grid and drop first if the 256 MiB cap is hit.

KJFK+KEWR collision memory (measured): **19 airport volumes, 2304 B
schematic**. Houses ≈ 72 B each. Full-set estimate (548 k KJFK +
80 k KEWR) ≈ **43 MiB**. Height = OSM `height`, else levels × 3.5 m,
else 12 m, on DEM. Tests: each named terminal / tower / hangar
HAT+segment, 10 k house sample, WTC era.

**Official geometry + lights for every shipped airport; imagery
correction layer; `docs/IMAGERY_SOURCES.md`.** Geometry and lights
always come from FAA NASR (Chart Supplement / NFDC, cycle
**2026-10-01**, retrieved **2026-10-18**) or national AIP AD 2
(DFS / NATS / ENAIRE / DGAC Ecuador / Aerocivil / CAAN) — never
Raster. OurAirports is a cross-check only. Every value carries
source + effective + retrieved. `check_airport_vs_faa` runs in CI
on all 18 airports. Where Raster disagrees (KLAS 26R/26L are MALS/
MALSF not ALSF-2; KLAX 24L is MALSR; KBOS 15R is MALSR / 33L is
ALSF-2; KLAS 08L/26R is 14835 ft), official wins;
`s376-raster-parity` flags the difference.

Imagery: sharper of the free public source vs Raster-local. Catalog
in `docs/IMAGERY_SOURCES.md` (public sources only). Known Raster
fallback: **KRNO** (only NAIP is free), **SKRG**, **VNKT**. Local-only
`s376-cache import-raster <dir> --out cache/<ICAO>`. Never commit RSI.

When imagery conflicts with FAA/AIP, `plan_corrections` /
`apply_correction_overlay` are drawn over packed tiles (fetch
`pack_lods` and `import-raster`): official surface, markings,
displaced-threshold arrows/bars, EMAS, and a blend-out of stale
paint or pavement. Each pack writes `cache/<ICAO>/corrections.txt`.
Doctor `imagery-official-<icao>` flags mismatches above the
accuracy tolerances and, when a pack exists, the lod0 geotransform.

**Queued after hangar priorities (do not interrupt):** once the
current KJFK fetch validates (`faa-coverage`, `faa-elev`, 12k shot),
run a near-field pass at `--near-m 0.3` (NYS/NYC 2024 ~15–30 cm)
over the airport so we beat Raster ~46 cm. **Do not** change the
in-flight fetch command to 0.3 m.

```bash
# AFTER current fetch validates — not now:
./build/s376-cache fetch KJFK --out cache/KJFK --dem 3dep --imagery nys \
  --near-m 0.3 --near-dem-m 1 --near-km 5 --approach-nm 10 --radius-km 35
```

Prior increment: per-airport imagery + local `import-raster`. SKRG
and VNKT are **Raster-local**. KRNO is **either** (NAIP ~0.6 m
roughly matches). Catalog also in `docs/SOURCES.md` /
`airport_imagery()`.

```bash
./build/s376-cache import-raster /path/to/Raster/VNKT --out cache/VNKT
./build/s376-cache import-raster DIR --dry-run
```

Read-only on the source. Imports only georeferenced GeoTIFF / JP2 /
JPEG+world / PNG+world that GDAL can open. Proprietary pack files are
counted as opaque; if nothing decodable is on disk the tool fails and
prints what it needs. Nothing from that folder is copied into git.
`scripts/check-no-rsi.sh` still applies.

Live fetch kinds added: `sanmateo` (KSFO), `kingco` (KSEA). Other
public sources in the table are download-then-import until a stable
ImageServer is wired.

Prior increment: fetch 2048 px / resume / `--plan` / faa-elev.
Prior: missiles, wizard, Classic skyline.

`s376-wizard` is a Dear ImGui + GLFW console on the Host IO /
operator PC. It talks to the channel IGs over a local UDP control
channel (`S376C1`, default `127.0.0.1:2376`) — **not** xT4, **not**
2300/2301, never the sim LAN from tests. Tabs: Channels (fps, GPU
temp/VRAM, RAM, host-packet age, errors, **Refresh channels**),
Display (heading/FOV/flip/warp/blend/gamma/patterns, saved to
config), Info (`git describe` + date; version×7 unlocks Classic
and Missiles), Logs (live tail). Refresh sends `op=refresh` →
`refresh_channels()` (clears Classic **and** missiles) then closes
and reopens each IG process. See `docs/WIZARD.md`.

**Classic NYC skyline easter egg** (hidden, session-only) plus
**disk-unlimited fetch tiers**.

Unlock: click the on-screen `s376-visual 0.1.0` label **7 times
within 15 s**. After the 4th click the toast counts down like
Android: "Click 3 more times to unlock Classic NYC skyline
(pre-2001)", then 2, then "1 more time", then "Classic skyline
unlocked". Era: Current vs Classic (pre-2001). Neutral labels.

Classic is **stylized** Lower Manhattan (hero lighting, aluminum-fin
facades, night windows, reflections, warning lamps, 1 WTC antenna).
Accuracy is **not** required for this egg. Classic **draws** only
inside the NYC geofence (`40.40–40.98 N`, `74.45–73.45 W`);
everywhere else is the normal Current world. Once unlocked it
**stays on for the session**. Reposition and leaving NYC do **not**
clear it — fly back to NYC and Classic is there again. It clears
only when the host closes and reopens the channels, or
**Refresh channels** in `s376-wizard` calls `refresh_channels()`.
Nothing is written to disk.

Collision / HAT uses the **same era-visible** geometry (Classic
volumes only while Classic is on *and* the eye is in the NYC box)
plus **every** OSM / schematic building via an always-resident
`solids.bin` grid — not the 8 k mesh bake cap, not LOD, not tile
paging. Host crash is host-side — see type-200 fields below.

### Missiles (hidden, visual only)

Same unlock and session lifetime as Classic. After unlock, turn
**Missiles On** on the IG HUD (second line under Era) or the wizard
Info tab. Still off until that explicit On. `refresh_channels()`
clears it.

**Trigger — existing host field, not a new xT4 type.** Config
`[egg]` (`config/s376-visual.toml`):

| `egg.ptt_source` | Host input | Default |
|---|---|---|
| `switch` (default) | Type **112** light-step, switch **255** (unused by airport lights 1–4). Rising edge: step goes from 0 to >0 | `ptt_switch=255` |
| `landing_bit` | Unused bit of type **109** `landing_mask` (lobes are bits 0–6) | `ptt_landing_bit=7` |
| `key` | Desk/demo keyboard | `ptt_key=m` |

Each press fires one missile from own-ship along the boresight
(heading + pitch). Cheap GPU points: body, smoke trail, fireball,
smoke plume, temporary scorch. Cap 4 live. **No host damage, no
flight-model effect, no new packet.** Type-200 HAT is unchanged.

**HARD SAFETY (Classic active only).** When the Classic skyline is
the *active* draw (session Classic **and** eye in the NYC box),
missiles are fully disabled inside **10 statute miles** of the WTC
site **40.7115 N, 74.0134 W** (16 093.44 m). Inside that disk: no
firing (neutral toast `Missiles unavailable in this area`). Any
in-flight missile whose next step would enter the disk is
**removed silently** before entering — no impact, no explosion, no
scorch. Trajectory is never allowed to reach the zone. No impact
FX may render on or near the classic towers. **Modern / Current
skyline: no restriction.**

`src/missiles.cpp`, `tests/test_missiles.cpp`.

**Disk-unlimited fetch tiers.** Owner: disk size is not a constraint;
the dataset may exceed Raster's 857 GB if quality improves (new SSDs
in the visual PCs). Quality and accuracy win over storage. **Runtime
RAM/VRAM on the 980 Ti still apply.**

`s376-cache fetch` metres/px default to **0 = native for the source**:

| LOD | Coverage | Imagery | DEM |
|---|---|---|---|
| 0 | 8 km ARP + 12 nm finals | NYS 0.15 / HRO 0.30 / NAIP 0.60 / S2 10 | 3DEP 1 / GLO-30 30 |
| 1 | 40 km | 4× native | 2× native |
| 2 | 50 km outer | 16× native | 8× native |

A 4000 px tile at NYS 0.15 m is 600 m on a side (more tiles, not
blurrier pixels). Operator `--near-m 1` on NYS stays 1 (no force
native). Pager still caps resident 2 GiB / 5 GiB. See
`docs/QUALITY.md`.

Road traffic (`3e99c97`), accuracy core (`702adb3`), and the scenery
checklist / raster-parity guard (`700659c`) are landed.

This increment: unit tests **129695 passed, 0 failed**. Doctor
`faa-*` + `imagery-official-*` pass for all 18 official airports.
Hangar re-fetch is still priority 1 — use the resume command below
(2048 px, do not pass `--max-export 4000`). Fetch is **streaming**:
one decoded tile in RAM, root DEM merged from disk (subsampled),
RSS logged every 16 tiles. Bound is 2 GiB regardless of tile count
(the e2f6af6 4 GB OOM was `dems` keeping every 2048² float grid).
KJFK `--near-m 0.3` is queued after the current resume validates.
For VNKT/SKRG/KRNO Raster-local:

```bash
./build/s376-cache import-raster /path/to/Raster/VNKT --out cache/VNKT
```

### Host crash / HAT (type 200)

The IG does **not** send a crash packet. Crash detection is host-side
on the existing 64-byte type-200 reply (`docs/PROTOCOL.md`):

| Words | Field | Host action |
|---|---|---|
| 10 | Valid | Ignore the sample if 0 |
| 22–23 | HAT ft (IEEE BE) | Radio altitude above DEM **or** a building roof. **HAT ≤ 0 (or below the host radio-alt threshold) = ownship in/on a solid** — this is what must trip crash |
| 28–29 | HOT / elev ft | MSL of that solid |

While Classic is on and the eye is in the NYC box, those fields use
the Classic BVH. Outside NYC, or after `refresh_channels()` / a
channel restart, they use Current. No GPU readback.

---

## Build on Linux Mint 22 / Ubuntu 24.04

```bash
git clone https://github.com/joeylawell/s376-visual.git
cd s376-visual
git checkout cursor/s376-visual-v1-6852   # or main once merged
./scripts/setup-mint.sh                   # apt + cmake + tests + synth KSEA
# already have packages:
./scripts/setup-mint.sh --skip-apt --no-desktop
```

Manual equivalent:

```bash
sudo apt install -y git cmake g++ libopenscenegraph-dev libgl-dev \
  libglew-dev libgdal-dev libglfw3-dev xvfb
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j"$(nproc)"
unset S376_ALLOW_SIM_LAN S376_ALLOW_LAN
ctest --test-dir build --output-on-failure
./scripts/check-no-rsi.sh
./build/s376-raster-parity --ours --icao KJFK
```

GL must be **compatibility**, not Core. Apt OSG 3.6.5 + NVIDIA 580
(980 Ti) or 595 (5060 Ti) both work when we leave the Linux context
version unset. Optional osgEarth: `scripts/build-osgearth.sh` →
`/opt/osgearth`. Windows: README (CMake + optional vcpkg OSG).

---

## Fetch a cache, then doctor / screenshot

The hangar KJFK pack was **mislocated** (~20 km SW; no lod0 north of
40.614 N; field is 40.62–40.66 N). **Delete it and re-fetch centered
on the FAA ARP** with NYS imagery:

```bash
# One-time: drop the failed 1.1 GB 702adb3 pack (old tile IDs / bboxes).
# After that, re-run the same fetch to resume — do not rm -rf again.
rm -rf cache/KJFK
./build/s376-cache fetch KJFK --plan --out cache/KJFK --dem 3dep --imagery nys \
  --near-m 1 --near-dem-m 1 --near-km 5 --approach-nm 10 --radius-km 35
./build/s376-cache fetch KJFK --out cache/KJFK --dem 3dep --imagery nys \
  --near-m 1 --near-dem-m 1 --near-km 5 --approach-nm 10 --radius-km 35
# 2048 px cap (do not pass --max-export 4000). Same command resumes.
./build/s376-cache bake-osm cache/KJFK   # if osm.json exists; runtime uses osm.bin only
./build/s376-doctor --cache-dir cache/KJFK --no-bench
# expect [pass] faa-kjfk, [pass] faa-coverage (ARP + every end in lod0)
# expect [pass] rss peak << 2 GiB

./build/s376-visual --screenshot /tmp/jfk-12k-mid_left.ppm \
  --cache-dir cache/KJFK --at KJFK --alt 12000 --hdg 036 \
  --channel mid_left --screenshot-wait 5
# expect: 1920×1080 P6+PNG, ortho on JFK/Rockaway (not flat green),
# no top garbage band, zero `invalid enumerant`, blue sky,
# runways as pavement not a white cross

# Joey video (after the pack is valid). Memory-capped; cars animate.
./build/s376-visual --cache-dir cache/KJFK --channel mid_left --approach KJFK:04R \
  --start-agl 2000 --gs 3.0 --kts 140 --fps 30 --record /tmp/jfk-04r.mp4
```

KEWR (same flags). Winter pack (second season, same sources for now):

```bash
./build/s376-cache fetch KJFK --imagery nys --season winter --out cache/KJFK
# writes cache/KJFK/winter
```

Local Raster compare (path stays on the caller's disk; nothing is
copied into git):

```bash
./build/s376-raster-parity --ours --icao KJFK
./build/s376-raster-parity /path/to/local/Raster/KJFK_KEWR --icao KJFK
# or drop a tree under ./raster-local/ (gitignored)
```

---

## Real-hardware results so far

| When | Box | Result |
|---|---|---|
| OOM on first KJFK screenshot | Mint, 16 GB, RTX 5060 Ti | **Fixed.** Pager + `osm.bin` + compressed DDS. Peak **36 MiB RSS** on a later doctor/screenshot (4 GB cap, no crash). Must also hold on i7-5930K + GTX 980 Ti 6 GB / 16 GB RAM. |
| Screenshot at **72df5f5** | same hangar GPU | Top ~7% **garbage band** (FBO 1920×1080 vs window 1920×1008; slave inherited window scissor). **16× `invalid enumerant`** (OSG 3.6 uploaded BC7 as `GL_COMPRESSED_RGBA_BPTC_UNORM`). Fixes landed in `3add51f` (FBO viewport/scissor/clear; BC7→DXT1 transcode). **Re-test after the re-fetch.** |
| KJFK cache on Mint | hangar pack | **Mislocated.** 14 lod0 tiles never reach north of 40.614 N. `region.toml` sat ~20 km SW because fetch packed from `dems.front()` (iy=0 = SW tile). Fix landed in `702adb3` (ARP-centered merge; throw if ARP/ends outside lod0). **Re-fetch with `--imagery nys` is the next hangar step.** |
| 8285de8 screenshot | Mint, NVIDIA 595.99, GL 4.6 compat | Valid-sized P6 but garbage at 1920×1008 (decorated window). FBO-at-channel-size landed later. |
| 88992c4 GL spam | Mint, 5060 Ti, apt OSG 3.6.5 | ~27k `detected OpenGL error` + SIGTERM hang. Compat context + shader lighting + SIGINT/`g_stop` landed. |

This cloud agent has no hangar GPU. Xvfb here often fails
`GLXBadFBConfig`. Visual proof is the hangar `--screenshot`.

---

## GTX 980 Ti / NVIDIA 580 notes

Target: i7-5930K + **GTX 980 Ti 6 GB Maxwell** / **NVIDIA 580** /
OpenGL, 60 Hz at one channel (1920×1080, 50×32.5). `render.quality =
"gtx980ti"` is the default; `"rtx"` may go higher.

- Linux **must not** request a Core profile. We leave
  `setGLContextVersion` unset so 580/595 give a 4.6 **compat**
  context. `setGLContextVersion("3.3")` with no mask was Core on 595
  and illegal for apt OSG's leftover FF StateSets.
- Scene lighting / fog / points are GLSL 120. Maxwell-unsafe modes
  (PointSprite, POINT_SMOOTH, FF LIGHTING/FOG) are stripped.
- OSG 3.6 does not treat BPTC as compressed — we transcode our BC7
  tiles to DXT1 before upload (980 Ti and 5060 Ti both get DXT1).
- RAM/VRAM caps: `ram_mb=2048`, `vram_mb=5120`. Never mesh a 4000²
  DEM; pager uses 96². Runtime OSM is `osm.bin` only.
- 980 Ti scenery caps: OSM **mesh** bake 8 k buildings; mesh nearest
  800 full / 2500 far. Collision is `solids.bin` (all footprints,
  ~50 MiB at 548 k). Night lamps follow OSM/public roads (cap 800);
  no 60 m ARP lattice.
- Skip the display-correction GPU pass when the ramp is identity.
- `gtx980ti`: HDR/atmosphere/markings/detail/buildings on; SSAO cheap
  wall darken; shadows **off** (no CSM yet).

---

## Open issues

| # | Item | Notes |
|---|---|---|
| 1 | **Hangar re-fetch KJFK** | Resume fetch (2048 px, `--plan` first). Confirm `faa-coverage` + `faa-elev`. Do not `rm -rf` if raw/tiles exist. |
| 2 | **Hangar 12k re-test** | After re-fetch: ortho, no top band, zero `invalid enumerant`, blue sky, pavement runways. Paste doctor RSS + shot path. |
| 3 | Host CSN movers | Type-100 CSN ≠ 1 counted, body ignored. 12 routes + marshaller poses are stored only. |
| 4 | Road traffic cars | **Landed.** 280-car GPU batch; OSM highways when baked. |
| 5 | Full ~30 k OSM **mesh** | Collision is every footprint (`solids.bin`). Draw still 800/2500. |
| 6 | Winter imagery vintage | `--season winter` writes `cache/<ICAO>/winter`; still the same NYS/NAIP URL. |
| 6b | Wire remaining public pulls | NJOGIS / MassGIS / Loudoun / Clark / LACO / OCPA / MDC / Hessen / EA / ICGC / IGM / IDECA are catalogued; fetch live today is nys/naip/hro/s2/sanmateo/kingco. |
| 7 | NY region pager | `data/regions/ny.toml` exists; KEWR tiles load only when nearby — not wired. |
| 8 | Ownship 109 beams | Decoded; no landing/taxi lobes, no beacon/strobe draw. |
| 9 | Type 131 display | Decoded; windows always on. |
| 10 | Type 124 runway condition | Decoded; unused. |
| 11 | Cloud catalog / cell size | 105/125–127 drawn as stand-ins. |
| 12 | CSM shadows | Toggle only. |
| 13 | Off-axis frustum | Shared eye + heading offset. |
| 14 | FO `eye_right_m` | Config exists, default 0. |

---

## Priority order (work this across runs)

1. **Hangar re-fetch + 12k screenshot** (items 1–2). Blocking for
   anyone looking at the picture. Fetch at **native NYS** (no
   `--near-m 1`). Keep CI green while it runs. **Do not** switch
   this run to `--near-m 0.3`.
2. **KJFK near-field 0.3 m** — only after item 1 validates. NYS/NYC
   2024 ~15–30 cm over the field so we beat Raster ~46 cm.
3. **Host CSN movers / marshallers** (3) — Raster had 12 moving-model
   routes. Road cars are done.
3. **Raise OSM building cap toward 30 k** with the existing
   near/far mesh split (5). Stay inside the 980 Ti RSS/VRAM caps.
4. **Winter vintage URL + NY pager** (6–7).
5. **109 beams, 131 display, 124 wet/snow, cloud catalog** (8–11).
6. Shadows / off-axis / FO eye only if the hangar still needs them.

FAA numbers always win. Do not invent packet types. Do not copy
Raster photos or `nxHostPacket.h`.

---

## Milestone: road traffic (Van Wyck / Belt / JFK / NJTP) (2026-10-18)

Owner: original sim shows small cars on roads (MSFS-style).

- `src/traffic.cpp`: class-weighted cars on motorway/trunk/primary/
  secondary. TOD density, night head/tail lamps, 7 km cull.
- Public alignments: Van Wyck, Belt Parkway, JFK Terminal + Access,
  NJ Turnpike, US-1/9 (OSM/public geography, not Raster extracts).
- GPU: one batched VBO, ≤ 280 cars, GLSL 120 lit body + unlit lamps.
  Fits the 980 Ti channel budget.
- `s376-cache osm` Overpass now asks for those highway classes.
  osm.bin version 2 stores roads; v1 caches still load.

## Milestone: KJFK/KEWR scenery parity + local Raster comparer (2026-10-18)

Owner: public checklist of everything Raster's 2023 KJFK_KEWR pad
had; local-only comparer; CI guard; FAA still wins.

- Checklist: `docs/PARITY.md` + `s376::scenery_parity_items`.
- Public stand-ins: 2022 taxi names C1–C6 / U1–U3 (no CC), signs,
  hold bars, T5, jetbridges, docking, parked, ATCT+beacon, trees,
  12 routes, marshallers, road-derived street lights (no lattice).
- `s376-raster-parity --ours | PATH` — positions via IEEE doubles
  that look like lat/lon. Refuses sim-LAN paths. Warns if PATH is
  this repo.
- CI: `scripts/check-no-rsi.sh` on every Linux job; unit tests
  `test_parity` / `test_repo_guard`.

## Milestone: FAA accuracy (KJFK/KEWR, cache centering) (2026-10-18)

Owner directive: airport data from FAA NASR / AIP AD 2 + NFDC,
cross-checked OurAirports; source + effective date on every value;
CI accuracy suite; datum/geoid/UTM; Raster-difference list.

- **KJFK / KEWR** rebuilt from **both FAA ends** (`src/faa.cpp`,
  `data/faa/{kjfk,kewr}.toml`). Vincenty heading+length. 13L is 200 ft.
  Displaced: 22R **3424**, 13R 2044, 31L 3264, 13L 907, 31R 1027, 4L 460.
  ALS: 04R/22L/13L ALSF-2+SFL, 31R MALSR, 13R RLLS, 04L/22R/31L NONE.
  PAPI 3.00° all eight JFK ends (13R two P4L; 31R P4R). EMAS DER 4R
  393×226 and DER 22L 405×226. KEWR NFDC displaced 2540.6 / 1440.9
  (owner 2543/1423 documented).
- **Accuracy suite** (`tests/test_accuracy.cpp`): end 3 m, heading
  0.1° (geodesic of the two FAA ends), threshold 3 m, ALS type, PAPI
  side/angle, imagery geotransform 1 m, DEM vs FAA elev 1 m. Doctor
  `faa-kjfk` / `faa-kewr` / `faa-coverage`.
- **Geodesy:** EGM96/EGM2008 undulation, orthometric↔ellipsoid, UTM
  round-trip, Vincenty inverse. Tested in `test_geo`.
- **Cache centering:** `fetch_and_pack` no longer packs from
  `dems.front()` (SW tile, iy=0). Root DEM is ARP-centered from
  packed tiles (`merge_root_dem_from_cache`, one subsampled tile
  at a time). Fetch **throws** if ARP or any runway end is outside lod0.
- **Streaming fetch:** decoded DEM/imagery are written and dropped
  per tile. Do not keep a `vector<DemGrid>` of 2048² tiles (that
  OOM-killed KJFK at 258 lod0 / 4 GB). Log `rss tile N/M now/peak`
  every 16 tiles. Bound: `kFetchRssBoundBytes` (2 GiB).
- **Re-fetch** (hangar KJFK pack was ~20 km SW; no lod0 north of 40.614):

```bash
rm -rf cache/KJFK
./build/s376-cache fetch KJFK --out cache/KJFK --dem 3dep --imagery nys
./build/s376-cache fetch KEWR --out cache/KEWR --dem 3dep --imagery nys
# native NYS 0.15 m / 3DEP 1 m; disk is free, pager still caps RAM/VRAM
./build/s376-doctor --cache-dir cache/KJFK --no-bench
# expect [pass] faa-kjfk, [pass] faa-coverage (ARP + every end in lod0)
```

NY shared outer box is `data/regions/ny.toml` (~40.40–40.98 N,
74.45–73.45 W). High-res stays per airport. Pager should load KEWR
only when nearby (not wired yet).

This agent: **14143** unit tests passed; `s376-doctor --no-bench`
`[pass] faa-kjfk` (63) and `[pass] faa-kewr` (48).

## Milestone: hangar 72df5f5 follow-up — tiles, FBO, BC1, sky (2026-10-18)

OOM is fixed on Mint / RTX 5060 Ti (rss peak 36 MiB, 4 GB cap, no
crash). The 12k mid_left shot was still wrong:

1. **Near field green.** `scan_cache_tiles` paged `cache/KJFK`,
   `lod0/`, `lod1/` directory roots as 96² meshes over the whole
   field. A 40 km `make_local_ground` patch sat **on top** of JFK
   (12 m vs DEM ~4 m). lod0 BC7 failed to upload (see 3) so only
   distant BC1 tiles showed. Fix: leaf `tY_X` only; skip local-ground
   when a tiled cache is present; `osm.bin` applies only to the
   matching ICAO; screenshot waits until near lod0 tiles are resident.
2. **Top ~7% garbage.** Window is 1920×1008; FBO is 1920×1080. Slave
   inherited the window viewport/scissor so the top 72 rows were
   never cleared. Fix: FBO pre-draw `glViewport`+`glScissor`+`glClear`
   of the full channel size; master viewport temporarily set to WxH.
3. **16× invalid enumerant.** OSG 3.6 does not treat BPTC as
   compressed (`glTexImage2D(GL_COMPRESSED_RGBA_BPTC_UNORM)`).
   `make_osg_safe` transcodes our mode-6 BC7 → DXT1 block-by-block
   (no 48 MB RGBA tile). 980 Ti / 5060 Ti both get DXT1 through OSG.
4. **Grey sky.** Altitude haze was `3.2e-6 + 1.8e-5*(alt/16k)` and
   the horizon term was `2.50*g^1.35` — ~70% fog at 10 nm. Now a
   weak haze, blue zenith/horizon, light painted cumulus.
5. **Glowing white runway cross.** Markings no longer get extra
   lighting and fade to pavement by ~4.5 km.

Re-test:

```bash
./build/s376-visual --screenshot /tmp/jfk-12k-mid_left.ppm --cache-dir cache/KJFK \
  --at KJFK --alt 12000 --hdg 036 --channel mid_left --screenshot-wait 5
# expect: imagery on JFK/Rockaway (not flat green), no top garbage band,
# zero `invalid enumerant`, blue sky, runways as pavement not a white cross
./build/s376-doctor --cache-dir cache/KJFK --no-bench
```

## Milestone: KJFK OOM — page tiles, osm.bin, RAM budget (2026-10-18)

Hangar Mint box (16 GB RAM, RTX 5060 Ti) OOM-killed
`./build/s376-visual --cache-dir cache/KJFK --screenshot … --at KJFK
--alt 12000 --hdg 036 --channel mid_left`: **anon-rss 13.9 GB**,
total-vm 22 GB. That also killed the host app.

Joey's on-disk pack (do not commit): lod0 ~1.0 GB (14 subdirs), lod1
29.6 MB, lod2, `osm.json` 436 MB, `osm.geojson` 150 MB, `imagery.dds`
787 KB, `elev.s376dem` 590 KB, **~1.6 GB total**. Causes: every lod0
4000² DEM meshed at full res (~512 MB/tile × 14) + optional BC→RGBA /
hillshade, and `try_load_osm` slurping the 436 MB JSON for every
public airport.

Fix (must also hold on i7-5930K + GTX 980 Ti 6 GB / 16 GB RAM):

- **Pager:** `scan_cache_tiles` + distance/frustum + `ram_mb=2048` /
  `vram_mb=5120` (`gtx980ti`). `load_cache_mesh` / `DemGrid::peek` /
  `load_subsampled` (`mesh_n=96`). Never mesh 4000².
- **Compressed upload:** `dxt::read_dds` keeps DXT1/BC7 blocks. No
  OSG `readImageFile` decode. `setUnRefImageDataAfterApply(true)`.
- **OSM:** `s376-cache osm` / `bake-osm` stream-parses JSON →
  `osm.bin` (`S376OSM1`). Runtime `try_load_osm` opens **only**
  `osm.bin`. `try_merge_osm_file` capped at 2 MiB (bake tools use
  `max_bytes=0`).
- **Doctor:** `rss` (peak/now/budget) and `cache-tiles` (naive vs
  paged). `s376-doctor --cache-dir cache/KJFK`.
- **Empty `--at` cache:** auto `cache/<ICAO>` or a clear WARN (no
  silent synth KSEA).
- **Screenshot black bar:** FBO slave does not inherit the master's
  1920×1008 viewport.
- **NYS/HRO:** `--imagery auto|nys|hro|naip|s2`. KJFK auto = NYS
  Latest ortho. Defaults now pull **native** NYS 0.15 m / NAIP 0.60 m
  (disk free; pager still caps RAM/VRAM). See `docs/QUALITY.md`.
- **Detail atlas:** generated 128² grass/asphalt/dirt/concrete with
  distance fade.

Re-test on the hangar GPU (after `s376-cache bake-osm cache/KJFK` if
`osm.bin` is missing):

```bash
./build/s376-doctor --cache-dir cache/KJFK --no-bench
# expect [pass] rss peak << 2 GiB, cache-tiles paged << naive
./build/s376-visual --screenshot /tmp/jfk-12k.ppm --cache-dir cache/KJFK \
  --at KJFK --alt 12000 --hdg 036 --channel mid_left
# must not OOM; 1920×1080 PNG, no ~7% top black bar
```

## Milestone: screenshot FBO + selected channel (2026-10-18)

Hangar GPU test of 8285de8 (Mint, RTX 5060 Ti, NVIDIA 595.99, GL 4.6
compat): build + 10788 tests (now **10840** after this fix) + zero `detected OpenGL error` + clean
SIGINT. `--screenshot` wrote a valid-sized P6 but **garbage** (top ~40%
black, grey/teal/purple stripe noise) at **1920×1008** (decorated
window, not the channel's 1920×1080). `--channel mid_left` logged
`channel far_right hdg=-35.25` because `dump_config` printed every
`[[channels]]` row. `--at` dumped empty `SharedState` (`encoding=le/vax`,
`datagrams=0`) even though the pose came from `--at`.

- **Shot FBO:** `OsgRenderer::capture` renders into a non-MSAA offscreen
  FBO at the **channel** WxH (default 1920×1080). Color is post-tonemap
  LDR **RGBA8**, `glPixelStorei(GL_PACK_ALIGNMENT, 1)`, vertical flip,
  RGB convert. Writes **PPM + PNG** (`stb_image_write` v1.16, public
  domain, `third_party/`). Warm-up: ≥8 frames, then until the OSG
  DatabasePager is idle or `--screenshot-wait` (default 3 s).
- **Self-test:** `--self-test-shot DIR` (CPU always; GL FBO solid
  40/80/160 + gradient when a context exists). CI runs the headless
  CPU path. `tests/test_image.cpp` covers write/read/pixels +
  `dump_config(..., {mid_left})`. This agent: **10840** unit tests,
  headless `--self-test-shot` writes PPM+PNG, `--diagnostics --channel
  mid_left` prints `selected=mid_left` and no `far_right`, `--at` logs
  `no host socket` and dumps the shot pose. Xvfb still `GLXBadFBConfig`.
- **Channel log:** `dump_config(cfg, selected)` prints `selected=name`
  and **only** those channel lines. `--diagnostics --channel mid_left`
  and the end-of-run dump both use it. Startup also logs
  `selected mid_left  hdg=0.000  1920x1080`.
- **`--at`:** no host socket unless `--demo`. End dump uses `shot_host`
  (`eyepoints=1`, `last_peer=shot`, pose fields). Datagrams stay 0.

Joey's 8285de8 PNG conversion (`refs/new-jfk-12k-v1.png`) is
**reference only — do not commit**. A real `s376-cache fetch KJFK` is
running on the Mint box; paste doctor bytes/time here when it finishes.

Re-test on the hangar GPU:

```bash
./build/s376-visual --self-test-shot /tmp/s376-shot-self
./build/s376-visual --screenshot /tmp/jfk-12k.ppm --at KJFK --alt 12000 --hdg 036 --channel mid_left
# expect 1920x1080 P6 + /tmp/jfk-12k.png, log `selected=mid_left`, no far_right
```

## Milestone: aerial / bank image quality (2026-10-18)

Joey's second Raster photo (steep bank over a coastal field) is
**reference only** — `refs/raster-bank-aerial.jpg`, gitignored. Do not
commit it. Verdict on Raster: "looks like poo poo." Beat it without
changing host behavior. Budget is one 980 Ti channel at 60 Hz (4-PC
deploy); the water/haze path is ALU-only, no extra pass.

- **Oblique ground:** globe texture 2048×1024 (was 720×360), mesh 160×80,
  trilinear + aniso 16 + LOD bias −0.35. Soft SDF coasts replace the
  old 10° grid and the "whole western Atlantic is land" box.
- **Water:** `render.water` (on for gtx980ti/rtx). Fresnel sky
  reflection, stable (no-time) normal-mapped waves, sun glint,
  shoreline mix from the land mask. Hillshade water is z < 1 m (was
  z < 20, which painted KJFK as ocean). `s376-cache osm` now also
  asks Overpass for `natural=water` / `coastline` (not meshed yet).
- **Atmosphere:** extra horizon haze (`uHaze`) that grows with
  altitude; sky mixes toward fog color at the horizon.
- **Shot:** `--at ICAO` (ARP) or `ICAO:RWY[:nm]`, plus `--alt FT`,
  `--bank DEG`, `--hdg DEG`, `--pitch DEG`. ICAO-only + high alt
  stands off opposite heading so the field is ahead.

`10788` unit tests passed. Headless `--at KJFK:04R:8 --alt 3000 --bank 30`
logs `3000ft  hdg 31.0  pitch -3.0  bank 30.0  8.00nm`. `--at KJFK --alt
12000 --hdg 036` is `40.4932,-73.9191  12000ft  hdg 36.0  pitch -1.5`
(south of Rockaway, looking at JFK).

Cockpit photos (captain far_left+mid_left, FO mid_right+far_right) of
KJFK / Rockaway at MCP ALT 12000 HDG 036 IAS 202 are also reference
only — `refs/raster-capt-12000-*.jpg`, `refs/raster-fo-12000.jpg`.
`--at KJFK --alt 12000 --hdg 036` stands ~11 nm SSW of the ARP, pitch
≈ −1.5° (ocean → barrier → bay → field, lots of sky). Local 40 km
land/water patches around each airport keep that view sharp without a
NAIP cache. Extra JFK terminals (T1/T7/hangar/cargo) so 12k has 3D
mass Raster lacks.

Hangar comparisons (photos are reference only):

```bash
./build/s376-visual --screenshot /tmp/jfk-bank.ppm --at KJFK:04R:8 --alt 3000 --bank 30 --channel mid_left
./build/s376-visual --screenshot /tmp/jfk-12k.ppm --at KJFK --alt 12000 --hdg 036 --channel mid_left
./build/s376-visual --screenshot /tmp/jfk-12k-left.ppm --at KJFK --alt 12000 --hdg 036 --channel far_left
./build/s376-visual --screenshot /tmp/jfk-12k-right.ppm --at KJFK --alt 12000 --hdg 036 --channel far_right
```

## Milestone: collimated 3-on-4 PCs (2026-10-18)

Joey: the display is a **collimated cross-cockpit**. From each seat you
only see two screens — captain far_left + mid_left, FO mid_right +
far_right; the mirrors hide the other side. The two middle projectors
show the **same front view** (a duplicated front channel, not a split).
So Raster's 3 channels (startup log: front 0, left +35.515, right
−35.25, 50×32.5) run on 4 PCs. This supersedes the earlier "four unique
headings / TO MEASURE" note.

| PC / `--channel` | Raster channel | Heading | Who sees it |
|---|---|---|---|
| far_left | left | +35.515° | captain outboard |
| mid_left | front | 0° | captain inboard |
| mid_right | front (duplicate) | 0° | FO inboard |
| far_right | right | −35.25° | FO outboard |

- Default TOML is four `[[channels]]` (`config/s376-visual.toml`).
  Production is **one process per PC**: `--channel mid_left` or
  `$S376_CHANNEL`. Aliases: `left`→far_left, `front`→mid_left,
  `right`→far_right, `front_fo`/`fo_front`→mid_right.
- Laptop 3-window: `config/channels-3.toml` (names front/left/right).
- Hangar 4-monitor desk (one PC): `config/desk-4monitor.toml`
  (IOS + L/F/R on DP-4 / DP-3 / HDMI-0 / DP-0).
- All four bind UDP 2300 and consume the **same** xT4 stream (host must
  broadcast / multicast / fan-out). `render.sync_to_host` (default true)
  draws only on a new type-100 eyepoint so the four boxes stay on the
  same host pose. `host.reply_master` (default `mid_left`) is the only
  process that sends type-200. `--reply-master` / `--no-hat`.
- Optional per-seat offset: `eye_right_m` / `eye_fwd_m` / `eye_up_m`
  (metres, aircraft body). Default 0 = true duplicate front. Commented
  `eye_right_m = 0.51` on the FO channels in the TOML.
- `gtx980ti` preset is now 60 Hz × **1** on 6 GB Maxwell (ssao on,
  shadows off). Budget was ×3 when one box drove three windows.
- Parity: channel geometry **ok**. Host-stream sync **ok**. HAT master
  **ok**. Per-seat eye **partial** (config exists, default off).

`10766` unit tests passed on this agent (g++ / OSG 3.6.5). Headless:
`HAT reply on  master='mid_left'  this=mid_left`; far_left logs
`HAT reply off`. Both mids dump `hdg=0`.

Hangar (four boxes, same TOML):

```bash
# on each projector PC — or S376_CHANNEL=mid_left
./build/s376-visual --config config/local.toml --channel far_left
./build/s376-visual --config config/local.toml --channel mid_left    # reply_master
./build/s376-visual --config config/local.toml --channel mid_right
./build/s376-visual --config config/local.toml --channel far_right
```

## Milestone: image quality (2026-10-18)

Joey's phone photo of Raster on 4R (day, two collimated channels) is
**reference only** — gitignored under `refs/` / `raster-*.jpg`. Do not
commit it. Goal: match Raster's crisp markings / blue sky / horizon,
beat the tiled grass, repeating asphalt, painted-dome clouds, and
blocky terminals. Budget at the time was 60 Hz × 3; now one channel
per 980 Ti (see the 3-on-4 milestone above).

- **Markings:** FAA AC 150/5340-1 from OurAirports geometry — threshold
  bars (8/12), 5×7 numbers/L/R/C, aiming points, TDZ, 36 m centerline,
  0.9 m edges, yellow hold-short pair. Geometry, not a texture, so they
  stay sharp at grazing angles. Aniso 16 on drapes. Rubber strip in the
  first 900 m.
- **Ground:** NAIP/S2 drape × land-class noise (grass/dirt) that
  tightens up close. `scripts/fetch-textures.sh` pulls CC0 Poly Haven
  1K JPGs into `cache/textures/` (never committed). Shader hash is the
  offline fallback. Allowlist: ambientcg.com, polyhaven.com/.org.
- **Buildings:** schematic terminals plus OSM `building=*` extrude
  (height / building:levels), cap 280, walls+roof mats. `s376-cache
  osm` Overpass query now includes `way["building"]`.
- **Sky:** analytic Preetham-like dome (40 km, bin −1) using local up
  (not ECEF Z), sun disc, moon + stars at night, EXP2 aerial
  perspective. Clouds keep hash-soft edges + sun wrap.
- **Lighting:** ACES in every forward shader. `render.quality`
  `gtx980ti` (default: hdr/atmosphere/markings/detail/buildings on,
  ssao+shadows off) vs `rtx` (ssao+shadows on). SSAO is cheap wall
  darkening. Shadows are a toggle only — no CSM yet (980 Ti budget).
- **Shot:** `s376-visual --screenshot PATH --at ICAO:RWY[:dist_nm]`
  (`KJFK:04R:0` lined-up looking at the numbers, `:3` = 3 nm / 3°).
  Raster FOV still 50×32.5; front 0 / left +35.515 / right −35.25.
- **KBOS** added (04R ALSF-2, 10006×150, hdg 35°) so we can tell
  whether Joey's photo is JFK or Logan. `all_public_airports()` is 6.

Hangar check (real GPU):

```bash
./build/s376-visual --screenshot /tmp/jfk-04r.ppm --at KJFK:04R:0 --channel mid_left
./build/s376-visual --screenshot /tmp/bos-04r.ppm --at KBOS:04R:0 --channel mid_left
./build/s376-visual --screenshot /tmp/jfk-04r-3nm.ppm --at KJFK:04R:3 --channel mid_left
```

This agent Xvfb often fails `GLXBadFBConfig`; the hangar box is the
visual check.

## Milestone: Overpass glob + high-res fetch (2026-10-18)

Joey's first real `s376-cache osm KSEA` died with
`curl: (3) bad range in URL position 47` — curl globbed `[out:json]`.
GET URLs now percent-encode `[ ] { } "`, every curl line uses `-g`,
and `osm` POSTs the QL (`--data-urlencode data@file`).
`curl_glob_safe` / `http_get_command` are unit-tested.

Fetch no longer pulls one 384×384 DEM + 768×768 NAIP (~20 m/px).
ImageServer exports are tiled at 4000 px with resume (skip existing)
and `--retry 5`. **Current** default pyramid (disk-unlimited; see
`docs/QUALITY.md`):

- lod0: 8 km ARP + 12 nm finals, **native** imagery / DEM, BC7 tiles
- lod1: 40 km, 4× / 2× native, BC1
- lod2: 50 km, 16× / 8× native, BC1 (GLO-30 / S2 outside CONUS)

`--dry-run` prints tile counts and a packed-size estimate. Pack size
is not capped against Raster's 857 GB. `--near-m` / `--near-dem-m` /
`--mid-m` / `--mid-dem-m` / `--far-m` / `--far-dem-m` / `--near-km` /
`--mid-km` / `--approach-nm` / `--max-export` override the targets.
The renderer drapes `lodN/tY_X` tiles. `s376-doctor` reports tile
counts. Resume + BC7 keep the 980 Ti (6 GB) **resident** path
streamable; paging, not downsample, is how we stay in 2/5 GiB.

## Milestone: GL compat + SIGTERM/Esc (2026-10-18)

Hangar box (Mint 22.3, RTX 5060 Ti, NVIDIA 595.99 open, apt OSG 3.6.5)
at 88992c4 flooded ~27k `detected OpenGL error` lines in 60 s and
ignored SIGTERM (`timeout 8` needed `kill -9`).

- Linux no longer calls `setGLContextVersion`. NVIDIA then gives a 4.6
  **compatibility** (legacy) context. `setGLContextVersion("3.3")` with
  no profile mask was CORE on this driver; apt OSG's FF StateSets are
  illegal there. Non-Linux still requests 3.3 + mask `0x2`
  (`COMPATIBILITY_PROFILE_BIT`).
- Scene lighting / fog / points are GLSL 120. Material, LightModel,
  LIGHTING, LIGHT0, FOG, ALPHA_TEST, POINT_SMOOTH, PointSprite, and
  TEXTURE_2D modes are stripped from our StateSets and from OSG's
  SceneView global defaults so a CORE context stays quiet.
- OSG `ONCE_PER_ATTRIBUTE` GL error checks are off unless `--gl-debug`
  / `render.gl_debug`. CI greps the headless and Xvfb logs for
  `detected OpenGL error`.
- SIGINT/SIGTERM set `g_stop`, the viewer `setDone`s, and
  `run_synth_sender` breaks on that flag (the old hang was the synth
  thread joining a 3600 s `--demo` with no stop). Esc still sets done.
  CI: `timeout -k 2 3` must not reach SIGKILL.

## Verified in this run

- `s376-tests`: **10725 passed, 0 failed** (KBOS 04R ALSF-2, shot pose
  `KJFK:04R:0` / `:3`, FAA marking vert delta, OSM `building=*` merge,
  quality presets `gtx980ti`/`rtx`, Poly Haven allowlist, plus previous
  suite).
- `--headless --at KJFK:04R:0`: pose 40.6253,-73.7762, 26 ft, hdg 31°,
  pitch 0. `--at KBOS:04R:3 --quality rtx`: 3 nm final, 987 ft, pitch −3°.
- `s376-doctor`: 0 fail, 1 warn (osgEarth). Now lists `cache-kbos`.
  mesh-build **1.58 ms / 26250 verts / 8728 lights** (six fields +
  markings + schematic terminals). This agent's Xvfb still cannot
  realize a GL window (`GLXBadFBConfig`); hangar GPU is the visual
  check (`--screenshot /tmp/jfk-04r.ppm --at KJFK:04R:0`).
- `timeout -k 2 3 --headless --demo --duration 3600`: exit **124**
  (SIGTERM honored; stopped after 178 frames). Previously hung 60+ s.
- `s376-doctor`: 0 fail, 1 warn (osgEarth). Reports `cache-ksea` 4.9 KiB,
  SKIP for unpacked KSFO/KLAS, `mesh-build` 0.17 ms / 4096 lights,
  `frame-{front,left,right}` CPU view + GPU share when DISPLAY works.
- `s376-visual --headless --demo --duration 2`: 40 datagrams / 40 eyepoints,
  KSEA 16L, env TOD 14.5 / vis 60760 ft.
- `s376-visual --headless --demo-weather --duration 2`: 40 datagrams,
  env precip=1/6 wind=18@200 lightning=1 vis=4000, fog select=0
  RVR 2400 / height 400, clouds lower 2500–6000 / upper 12000–16000 /
  top on, storm0 CSN 28 at 47.42,-122.308 precip 1/7 lightning 2.
- This agent Xvfb cannot realize a GL window (`GLXBadFBConfig`);
  cloud/storm draw path compiled and is unit-tested. Hangar box has
  the real GPU.
- `s376-visual --demo` under Xvfb: OSG window "front" 1920×1080, 50×32.5 FOV,
  procedural globe, camera on the eyepoint.
- `s376-synth --dest 10.1.95.1:2300` and `192.168.173.10:2300`: refused.
- `s376-cache synth-ksea --out /tmp/s376-ksea --size 64`: **18 608 bytes**
  (BC1 + `.s376dem`); `info` samples KSEA ARP at 433 ft; `--out 10.1.95.1:2300`
  refused.
- GDAL 3.8.4 PASSes.
- GitHub Actions `ci / linux` on `d3f6dfc` (HAT + KSEA + cache): **green**
  (ubuntu-24.04, 1984 tests, doctor, headless demo, sim-LAN refusal,
  `s376-cache` smoke). Optional osgEarth job is `workflow_dispatch` only.

## Standing rule: Raster XT4 parity

Match the original RSI Raster XT4 on this 737-300: same host fields,
same visual features, same HAT replies. Allowed deltas: lighter GPU,
sharper image (MSAA / AF / crisp points), cleaner code. Do not invent
scenery or weather the host does not send. Checklist:
[docs/PARITY.md](docs/PARITY.md). Gaps stay listed there until closed.

## Milestone: 4-monitor desk + sharpness (2026-10-18)

`config/desk-4monitor.toml` pins IOS at x=0 (DP-4), left 1920 (DP-3),
front 3840 (HDMI-0), right 5760 (DP-0). Same ViewAngles as Raster.
`render.msaa=4` and `render.aniso=16` (GL 3.3 / Maxwell). Type-124
`tod_select` 0–3 now snaps sun/sky to day/dusk/dawn/night.

## Milestone: type-124 / 128 weather (2026-10-18)

Honor the **already-decoded** Raster weather fields. Do not invent
cloud or storm-cell layouts (`nxHostPacket.h` is gitignored and is
not on this agent box).

- Type 124 `precip_type` 1/2/3 + `precip_intensity` 0–10 → 360 GPU
  points around the eyepoint (rain / snow / hail), GL 3.3.
- Type 124 wind speed/dir → precip drift (FROM convention).
- Type 124 lightning trigger (words 10–11) → 4-frame sun/sky flash
  on edge change.
- Type 128 RVR still caps vis; fog height thickens EXP2 when last
  HAT is below the deck + 200 ft.
- `--demo-weather` / `s376-synth --weather` emit those packets only.

## Milestone: cloud layers + storm cells (2026-10-18)

Clean-room types **105 / 125 / 126 / 127** from the hangar reference
header (read only, **never committed**). Field tables in
[docs/PROTOCOL.md](docs/PROTOCOL.md).

- Lower/upper: ceiling + top MSL, select −1=off, vis-above, layer wind.
  Cheap GL 3.3 stacked discs + 8 cards around the eyepoint (GTX 980 Ti
  budget: a few hundred tris × 3 channels).
- Top layer: region+select only; 25–28 kft stand-in (original heights
  lived in the IG catalog).
- Fog × 2.4 when the eye is inside a deck.
- Storm select 0–29, ≤3 active, CSN 28–30 pose from type 100.
  Lightning 1=random, >1=edge. Size is not on the wire — 1.5 nm ×
  intensity stand-in (PARITY partial).
- `--demo-weather` now sends IMC decks + one cell south of KSEA.

See [docs/PARITY.md](docs/PARITY.md).

## Milestone: KLAX / KJFK layouts (2026-10-18)

Same generator as KSEA: OurAirports runway ends, FAA-charted ALS
(ALSF-2 on KLAX 24R/24L/25R/25L and KJFK 04L/22R/13L/13R/31L), schematic
taxi/apron, GPU lights on packet-112 switches 1–4. `data/airports/{klax,kjfk}.toml`
holds the public numbers. `all_public_airports()` later grew to six
with KBOS (image-quality round).

## Milestone: KSEA earth file (2026-10-18)

`data/earth/ksea.earth` is the osgEarth path: GDAL elevation and imagery
from `s376-cache` (`cache/ksea/elev.tif`, `imagery.tif`) plus an OGR
`feature_geom` layer on `osm.geojson`. `write_cache` now emits those
GeoTIFF sidecars when GDAL is present. If osgEarth is missing or the
earth file fails to load, the IG uses the procedural globe + local DEM
drape. `data/earth/s376.earth` stays the Esri-dev globe. GL context
request is 3.3 compatibility (not Core).

## Milestone: Mint setup + osgEarth scripts (2026-10-18)

`scripts/setup-mint.sh` installs apt deps, builds with g++, runs tests,
packs a synth KSEA cache, and writes `s376-visual-demo` /
`s376-visual-3ch` desktop launchers. `scripts/build-osgearth.sh` builds
osgEarth 3.7.2 into `/opt/osgearth` (no FASTDXT) and re-cmakes the IG
so `S376_HAS_OSGEARTH` turns on. Renderer stays GL 3.3-compatible
(Maxwell GTX 980 Ti + hangar RTX 5060 Ti). `s376-cache geojson ICAO`
writes a FeatureCollection for the earth-file airport layer.

## Milestone: doctor size / perf (2026-10-18)

`s376-doctor` lists on-disk `cache/<icao>` bytes for each public
airport (and any extra packed dir) plus `frame-<channel>` times.
CPU `make_channel_view` cost is always measured. GPU CompositeViewer
ms/frame (tiny 160×90 windows, share of the composite) runs only when
`XOpenDisplay` succeeds so CI stays headless. `--no-bench` skips the
timing section. `mesh-build` is reported separately.

## Milestone: KSFO / KLAS layouts (2026-10-18)

`ksfo_public()` and `klas_public()` match the KSEA generator: OurAirports
runway ends, FAA-charted ALS (ALSF-2 on 28R / 26R / 26L), schematic
taxi/apron offsets, GPU lights on switches 1–4. The renderer loads all
three fields. `s376-cache osm ICAO` pulls Overpass `aeroway=*` (ODbL)
into `cache/<ICAO>/osm.json` (gitignored). `data/airports/{ksfo,klas}.toml`
holds the public numbers. Unit tests cover four runways each, ALS types,
OSM merge, and mesh fans.

## Milestone: real GLO-30 / 3DEP + NAIP fetch (2026-10-18)

`s376-cache fetch ICAO` downloads USGS 3DEP (CONUS) or Copernicus GLO-30
and NAIP / Sentinel-2 cloudless, then packs a LOD pyramid (BC7 + mips
near the field, BC1 farther out). Verified live: 8 km KSEA 3DEP+NAIP,
ARP sample 391 ft MSL, packed tiles ~1.4 MiB after dropping raw COGs.
`--dry-run` prints allow-listed URLs. Licences in `docs/SOURCES.md`.
Downloaded rasters stay under `cache/` (gitignored). Synth `--lods`
still works offline for CI.

## Milestone: real HAT / HOT (2026-10-18)

`HostReceiver` samples `DemHeightSource` at the eyepoint. On the synth
KSEA pad, elev ≈ 433 ft and HAT ≈ 14 ft (eye height). Off the DEM the
old stand-in (elev=0, hat=alt) is still sent so the host is never
starved. Live loopback test builds a type-113 query and reads the
64-byte type-200 reply.

## Milestone: KSEA airport + GPU lights (2026-10-18)

`s376::ksea_public()` builds three OurAirports runways (16L threshold
matches the synth takeoff) plus schematic taxi connectors. Edge,
centreline, threshold, and ALSF-2/MALSR approach points are placed from
published FAA/ICAO spacing. A GPU `GL_POINTS` pass scales colour/alpha
by packet-112 steps 0–5 on switches 1–4 (ours, not RSI's). Synth sends
those switches at step 5 so the demo is lit.

## Milestone: local DEM + cache (2026-10-18)

Portable `.s376dem` + BC1/DXT1 DDS + `s376-cache`. GDAL GeoTIFF in when
`libgdal-dev` is present (`S376_HAS_GDAL`). Renderer drapes the packed
region (or a synth 25 km KSEA box) on the globe. osgEarth-from-source
recipe is in the README (Mint 22 / Ubuntu 24.04, tag `osgearth-3.7.2`).
Every-PR CI does **not** compile osgEarth; optional
`.github/workflows/osgearth.yml` is `workflow_dispatch` only.

## Milestone: three channels (2026-10-17)

CompositeViewer opens every enabled `[[channels]]` entry. One display tiles
left|front|right (verified under Xvfb 1920×1080: three 640×360 windows at
x=0/640/1280, heading offsets 35.515 / 0 / −35.25). `--channel front|left|right`
is the single-window fallback. Shared eyepoint; each view rotates by the
TOML heading offset (RSI ViewAngles). `select_channels` / `layout_channels`
are unit-tested.

## What's done and working

- Engine choice documented: **osgEarth on OSG 3.6**, with a procedural OSG
  globe until osgEarth is built from source. See `docs/DESIGN.md`.
- Repo skeleton: CMake, MIT license, clang-format, GitHub Actions CI on
  `ubuntu-24.04`, `.gitignore` that blocks RSI/CAE/captures.
- Clean-room xT4 decoder in `include/s376/xt4.hpp` + `src/xt4.cpp`.
  Autodetects word order (le/be) and float format (VAX F/D vs IEEE).
  Decodes eyepoint, environment (TOD, vis, precip, wind, lightning),
  fog, light-switch steps 0–5, ownship lights, display control, HAT query.
- HAT reply (`xt4::build_hat_response`) matches what the S376 host reads
  (64-byte big-endian IEEE). Host-side conversion is unit-tested.
- `s376-synth` sends a KSEA 16L takeoff to a destination that must pass
  the safety check.
- Host socket binds `0.0.0.0:2300` by default (overridable). Replies on
  2301. Built into the IG (no bridge process).
- TOML config: four projector PCs mapping Raster's three channels
  (`config/s376-visual.toml`). `host.reply_master` + `render.sync_to_host`.
  Laptop 3-window: `config/channels-3.toml`.
- Logging (`-v`, `--debug`, rotating file under `logs/`).
- `s376-doctor` / `--diagnostics` (cache bytes per airport, frame time
  per channel).
- Minimal OSG renderer: WGS84 globe, host-driven camera, shader fog/sun,
  precip points, cheap cloud decks (125–127), storm cells
  (105 + CSN 28–30), lightning flash. Local DEM patch + BC1 drape.
  osgEarth earth file is loaded when the lib is present. Linux GL
  context is legacy-compat; `--gl-debug` re-enables OSG error checks.
- `s376-cache`: `fetch` / `osm` / `synth-ksea --lods` / `from-geotiff` /
  `info`. Tiled 3DEP+NAIP (or GLO-30+S2) LOD pyramid, BC1/BC7 mips.
  `osm` POSTs Overpass QL (`-g`, percent-encoded GET fallback).
- `s376-wizard`: operator console (ImGui + GLFW). Local `S376C1`
  control on loopback. Channels / Display / Info / Logs. Refresh
  channels closes and reopens IGs and clears Classic + missiles.
  `docs/WIZARD.md`.
- Hidden missiles egg: PTT on type 112 switch 255 (or type 109 bit 7,
  or desk M). Visual only. Classic-active 10 sm WTC keep-out.
- Airport rebuild plan: `docs/AIRPORTS.md`. Terrain sources: `docs/SOURCES.md`.
- Safety: `10.1.95.0/24` and `192.168.173.0/24` refused unless
  `host.allow_sim_lan` / `S376_ALLOW_SIM_LAN=1`. Tests never set that.

## Half-done

| Item | Path | What's left |
|---|---|---|
| osgEarth tiles | `data/earth/ksea.earth` | File is in. Build osgEarth on the hangar box (`scripts/build-osgearth.sh`) and confirm the cache GeoTIFFs drape |
| Cloud / storm packets | `docs/PARITY.md` | 105/125–127 decoded and drawn. Remaining: catalog type table, real cell sizes, top-layer heights |
| HAT off-DEM | `sample_hat` | Stand-in elev=0 when the point is outside the packed region |
| More airports | `docs/AIRPORTS.md` | KSEA / KSFO / KLAS / KLAX / KJFK / KBOS are in. Next: EGLL / EDDF |
| Per-seat FO eye | `eye_right_m` | Config exists, default 0 (collimated shared eye). Uncomment 0.51 m if Joey wants it |
| Off-axis frustum | camera is rotated by heading offset | Shared eyepoint + asymmetric projection later |
| High-res fetch on hangar | `s376-cache fetch KSEA` / `KJFK` | Defaults are now **native** NYS/NAIP (no `--near-m 1`). Disk free; pager still caps RAM/VRAM |
| Screenshot on hangar GPU | `--screenshot --at` | FBO+PNG+warmup + viewport inheritance fix landed. Re-run 12k **with bake-osm** and confirm no OOM / no top black bar |

## Next 5 concrete steps (in order)

1. **Hangar GPU re-test (tiles + FBO + sky):** screenshot command in
   the 72df5f5 follow-up milestone. Confirm JFK has ortho (not green),
   no top band, zero GL invalid enumerant, blue sky, pavement runways.
2. **Close remaining PARITY gaps**: 109 beams, 131 display, 128
   patchy/speed-cue, 124 runway condition, PAPI/taxi, cloud type
   catalog, real storm-cell sizes.
3. **Asymmetric / off-axis projection** if ViewAngles overlap is not enough.
4. **EGLL / EDDF** layouts.
5. **FO `eye_right_m`** only if the duplicated front needs a seat offset.

## How to build / run / test (Linux Mint 22)

```bash
sudo apt install -y git cmake g++ libopenscenegraph-dev libgl-dev libglew-dev libgdal-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j"$(nproc)"
unset S376_ALLOW_SIM_LAN S376_ALLOW_LAN
ctest --test-dir build --output-on-failure
./build/s376-cache synth-ksea --out cache/ksea
./build/s376-cache fetch KSEA --dry-run
./build/s376-cache osm KSEA --dry-run
./build/s376-doctor -c config/s376-visual.toml
./build/s376-visual --headless --demo --duration 3 --bind 127.0.0.1:2300
./build/s376-visual --headless --demo-weather --duration 3 --bind 127.0.0.1:2300
# production: one projector PC. --channel front aliases mid_left.
./build/s376-visual --demo --cache-dir cache/ksea --channel mid_left
./build/s376-visual --demo-weather --channel mid_left
# laptop 3-window (front/left/right names):
./build/s376-visual --demo --config config/channels-3.toml
# Ctrl-C / SIGTERM must exit within a frame. --gl-debug re-enables OSG GL checks.
```

Windows: see README (CMake + optional vcpkg OSG). `--headless` and the
tests do not need OSG.

## Known issues

- osgEarth is **not** in Ubuntu 24.04 apt. v0.1 is useful without it
  (procedural globe + local DEM). Source-build recipe is in the README.
- IEEE packers assume a little-endian CPU (every hangar box we have).
- HAT is sampled from the local DEM when the eyepoint is on it.
  Off-grid replies still use elev=0 / hat=alt.
- `s376-doctor` tries to bind `127.0.0.1:2300`; if the IG is already
  running that check WARNs.
- No capture/replay yet (the old bridge had it). Add a pcap writer only
  for loopback, and never commit the files.
- Do not copy `nxHostPacket.h` or any RSI path into this tree.

## Design decisions

- **C++17 / CMake**, not Python. The IG is the realtime process.
- **Clean-room decoder.** Field layouts are in `docs/PROTOCOL.md` in our
  own words. Test vectors are synthetic packets we built, including
  golden hex dumped from the s376bridge Python builders (also synthetic).
- **osgEarth is the engine of record**; OSG 3.6 from apt is the v0.1
  renderer so CI and a Mint box work the same day.
- **One process.** Decoder + socket + renderer. Synth is a tiny extra
  binary for bench work.
- **Safety by default.** Production sets `allow_sim_lan` explicitly.

## Layout

```
include/s376/     public headers
src/              core + s376-visual / s376-synth / s376-doctor / s376-cache / s376-wizard
tests/            assert-style unit tests (no Catch2)
config/           default TOML
data/earth/       osgEarth earth file
data/airports/    public KSEA / KSFO / KLAS / KLAX / KJFK TOML
docs/             DESIGN PROTOCOL AIRPORTS SOURCES PARITY
```
