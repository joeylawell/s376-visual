# s376-visual

Open-source image generator for the CAE Boeing 737-300 full-flight simulator
(UAL ship S376). It replaces RSI Raster XT4: the host (an emulated VAX) sends
visual datagrams to UDP **2300**, and this process draws the out-the-window
scene. The xT4 reader lives **inside** the IG. There is no separate bridge.

v0.1 is the foundation: decoder, host socket, HAT reply, TOML channel config,
logging, diagnostics, a synth sender, and a single-window globe (OSG now,
osgEarth when you build it).

This repository is public. It does **not** contain RSI or CAE files, ICD text,
textures, databases, binaries, or packet captures.

## Requirements

| | Linux Mint 22 / Ubuntu 24.04 | Windows 10/11 |
|---|---|---|
| Compiler | g++ 13+ or clang++ 16+ | MSVC 2019+ or clang-cl |
| Build | CMake 3.16+ | CMake 3.16+ |
| Renderer | `libopenscenegraph-dev` (3.6.5) | OSG via vcpkg (optional) |
| Streamed tiles | osgEarth built from source (optional) | osgEarth via vcpkg (optional) |
| GPU | NVIDIA recommended; any GL 3.3 card runs the procedural globe | same |

## Linux Mint 22 install

One command on the hangar box (apt, build, tests, synth KSEA cache, desktop launchers):

```bash
./scripts/setup-mint.sh
```

Or by hand:

```bash
sudo apt update
sudo apt install -y git cmake g++ libopenscenegraph-dev libgl-dev libglew-dev libgdal-dev libglfw3-dev

git clone https://github.com/joeylawell/s376-visual.git
cd s376-visual
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The renderer stays **GL 3.3-compatible** (apt OSG 3.6 compatibility profile) so the same binary runs on the hangar RTX 5060 Ti and a Maxwell GTX 980 Ti. Do not force a Core profile.

osgEarth (optional, not in Ubuntu 24.04 apt):

```bash
./scripts/build-osgearth.sh          # installs to /opt/osgearth and rebuilds
```

Use **g++**, not the default clang: Ubuntu 24.04 / Mint 22 clang often lacks a
matching `libstdc++` and the link fails with `cannot find -lstdc++`.

Binaries land in `build/`: `s376-visual`, `s376-synth`, `s376-doctor`,
`s376-cache`, `s376-wizard`, `s376-tests`. The operator console
(`s376-wizard`) is documented in [docs/WIZARD.md](docs/WIZARD.md).

### Run without a VAX (demo)

```bash
./build/s376-visual --demo --channel mid_left
```

A synthetic 737 takes off from KSEA 16L. Production is **one process per
projector PC** (`--channel far_left|mid_left|mid_right|far_right`, or
`$S376_CHANNEL`). The two middle PCs show the same Raster front frustum
(0°); far_left is Raster left (−50°), far_right is Raster right
(+50°); all 50° × 32.5° at 1920×1200 (16:10; `resolution = "WxH"`).
far_left `flip_vertical` is a projector framebuffer mirror only.
`--channel front` still
aliases `mid_left`. Omit `--channel` (or `--channel all`) to tile every
enabled window on a laptop. `--channel all --record` / `--screenshot`
writes a stitched Left|Front|Right strip. `config/channels-3.toml` is
the three-name laptop alternative.
`--headless` runs the same path with no GL.
`--demo-weather` adds type-124 rain/wind/lightning, type-125/126/127
cloud decks, type-128 fog, and one type-105 storm cell (host fields
only; no invented packet types). Ctrl-C / SIGTERM exits within a frame;
Esc closes the viewer. `--gl-debug` turns OSG GL error checks back on.

### Run against SIMH (emulated host)

On the IG PC, with the visual LAN isolated:

```bash
# production only — allows replies to 192.168.173.x
export S376_ALLOW_SIM_LAN=1
./build/s376-visual --config config/local.toml --channel mid_left
```

`config/local.toml` (gitignored) should set `host.allow_sim_lan = true` and
keep `bind = "0.0.0.0:2300"`. The IG replies on UDP **2301**.

Default bind is `0.0.0.0:2300`. Tests and CI bind loopback and **never**
contact `10.1.95.0/24` or `192.168.173.0/24`.

### Two-process bench (loopback)

`--demo kjfk` starts `s376-fakehost` in-process on 127.0.0.1:2300/2301
(six-mile-final, 6 nm). Or run the stand-in yourself (loopback only):

```bash
./build/s376-visual --bind 127.0.0.1:2300 --reply-port 2301 --channel mid_left
# other terminal
./build/s376-fakehost --profile kjfk-04r --six-mile-final --start 6nm --fps 60
```

```bash
./build/s376-visual --bind 127.0.0.1:2300 --channel mid_left
# other terminal
./build/s376-synth --dest 127.0.0.1:2300 --rate 20 --encoding le/vax
# same plus type-124 rain and type-128 fog:
./build/s376-synth --dest 127.0.0.1:2300 --weather
```

### Flags

| Flag | Meaning |
|---|---|
| `-c / --config PATH` | TOML file (see `config/s376-visual.toml`) |
| `--bind HOST:PORT` | override listen address |
| `--channel NAME` | `far_left`/`mid_left`/`mid_right`/`far_right`/`all` (aliases `left`/`front`/`right`/`front_fo`). Default: all enabled, or `$S376_CHANNEL`. `--channel all --record`/`--screenshot` = Left\|Front\|Right strip |
| `--reply-master NAME` | only this `--channel` sends the type-200 HAT reply (default `mid_left`) |
| `--reply-port PORT` | type-200 HAT reply port (default `host.reply_port` = 2301) |
| `--mem-budget MB` | CPU resident cap (overrides `render.ram_mb`; 980 Ti = 2048) |
| `--cache-dir PATH` | packed DEM/imagery from `s376-cache` |
| `--headless` | decoder + socket, no GL |
| `--demo` | in-process synth to loopback (CI) |
| `--demo kjfk` | fakehost loopback 60 Hz KJFK 04R (`gtx980ti`, `mid_left`, `--tod 12`, six-mile-final, 6 nm) |
| `--six-mile-final` | `--demo kjfk`: gear down / flaps 30 from frame 1 (default) |
| `--start DIST` | `--demo kjfk` start: `6nm` (default), `6sm`, `10nm`, `11km` |
| `--demo-weather` | demo plus 124/125/126/127/128 weather and a type-105 storm |
| `-v` / `--debug` | verbose / per-packet hex |
| `--gl-debug` | OSG per-attribute GL error checks (off in Release) |
| `--quality NAME` | `gtx980ti` (default, 2 GB RAM / 5 GB VRAM cap) / `rtx` / `custom` — see `docs/QUALITY.md` |
| `--bench` | fly 04R and print GPU ms + a pasteable `SUMMARY  s376 --bench …` line |
| `--offscreen` | pbuffer first (no projector window); shots still use the FBO |
| `--screenshot PATH` | write the first channel (offscreen FBO at channel WxH, default 1920×1200, PPM+PNG) after warmup and exit |
| `--screenshot-wait SEC` | settle time before capture (default 3; pager idle or this) |
| `--self-test-shot DIR` | render a known solid/gradient, check pixels, write PPM+PNG |
| `--approach ICAO:RWY` | scripted straight glidepath to the landing threshold (`KJFK:04R`) |
| `--start-agl FT` / `--gs DEG` / `--kts KT` / `--fps N` | approach start AGL (2000), glidepath (3.0), groundspeed (140), record fps (30) |
| `--record PATH` | fly `--approach` offscreen (FBO) to `PATH`. `.mp4` pipes rgb24 into ffmpeg (one frame in RAM). If ffmpeg is missing, dumps `PATH-frames/frame-%06d.png` and prints the mux command. Cars step at `1/fps`. Uses the 980 Ti RAM/VRAM cap. `--headless` still opens GL |
| `--at ICAO[:RWY[:nm]]` | pose the eye (`KJFK:04R:0` lined-up, `KJFK:04R:8`, or `KJFK`). No host packets needed |
| `--alt FT` / `--bank DEG` / `--hdg DEG` / `--pitch DEG` | override the shot pose |
| `--diagnostics` | dump config and build options |
| `--doctor` | environment checks (local only) |

Compare to Joey's Raster 4R photo on the hangar GPU (photo is reference
only — never commit it):

```bash
./build/s376-visual --screenshot /tmp/jfk-04r.ppm --at KJFK:04R:0 --channel mid_left
./build/s376-visual --screenshot /tmp/bos-04r.ppm --at KBOS:04R:0 --channel mid_left
./build/s376-visual --screenshot /tmp/jfk-bank.ppm --at KJFK:04R:8 --alt 3000 --bank 30 --channel mid_left
./build/s376-visual --screenshot /tmp/jfk-12k.ppm --at KJFK --alt 12000 --hdg 036 --channel mid_left
./build/s376-visual --screenshot /tmp/jfk-12k-left.ppm --at KJFK --alt 12000 --hdg 036 --channel far_left
./build/s376-visual --screenshot /tmp/jfk-12k-right.ppm --at KJFK --alt 12000 --hdg 036 --channel far_right

# Joey video — straight 3° to 04R touchdown, cars animating (Mint RTX 5060 Ti):
./build/s376-visual --cache-dir cache/KJFK --channel mid_left --approach KJFK:04R \
  --start-agl 2000 --gs 3.0 --kts 140 --fps 30 --record out.mp4
# Wraparound strip (Left|Front|Right) at Raster 50×32.5 / 1920×1200:
./build/s376-visual --cache-dir cache/KJFK --channel all --approach KJFK:04R \
  --start-agl 2000 --gs 3.0 --kts 140 --fps 30 --record wrap.mp4
```

On Joey's Mint (repo `~/s376-visual`, cache `~/s376-visual/cache/KJFK`):

```bash
# One-shot 04R review + pasteable --bench (pbuffer, no projector windows):
scripts/hangar-review.sh

# Unattended hours-long queue (resumable; start the moment Mint is back):
scripts/hangar-queue.sh
# usage: scripts/hangar-queue.sh [--from NN] [--only NN] [--list] [--dry-run] [--root DIR] [--out DIR] [--kjfk DIR] [--all-airports]
# Default is KJFK-only (demo flight). KEWR / other US fields: --all-airports.
```

`s376-doctor` never sends a packet off the machine. It reports on-disk
`cache/<icao>` size for KSEA / KSFO / KLAS, **peak RSS vs `ram_mb`**,
a naive-vs-paged tile estimate when `--cache-dir` is set, and a
per-channel frame-time bench (CPU view cost always; GPU ms/frame when
`DISPLAY` is set).

`--at ICAO` with an empty `cache_dir` auto-selects `cache/<ICAO>` when
that pack exists; otherwise it **warns** and falls back to a synth
KSEA DEM (it will not silently look like JFK). The IG pages lod0
tiles and never opens `osm.json` — run `s376-cache bake-osm DIR` first.

## Windows

1. Install [CMake](https://cmake.org/download/) and Visual Studio 2022 (C++).
2. Optional renderer: `vcpkg install openscenegraph:x64-windows` and pass
   `-DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake`.
3. From a VS developer prompt:

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Without OSG the core, tests, synth, and `--headless` IG still build.

## Local DEM + imagery (no osgEarth required)

The IG drapes a local height field even when osgEarth is absent. Pack a
region once, offline:

```bash
# synthetic 25 km Sea-Tac box (no download) — CI / laptops
./build/s376-cache synth-ksea --out cache/ksea --lods

# real, freely licensed data (hangar box with a network)
# native NAIP ~0.60 m / 3DEP 1 m on 8 km + 12 nm finals, 50 km outer
./build/s376-cache fetch KSEA --out cache/ksea
./build/s376-cache fetch KSFO --out cache/ksfo
# preview URLs + packed-size estimate (no download):
./build/s376-cache fetch KSEA --dry-run
# Overpass taxi/apron (POST; [ ] never reach curl globbing) → osm.bin:
./build/s376-cache osm KSEA --out cache/ksea
./build/s376-cache bake-osm cache/KJFK

./build/s376-visual --demo --cache-dir cache/ksea
```

Output is `elev.s376dem` (portable grid) + `imagery.dds` (BC1/DXT1 GPU
texture) + `region.toml`. Disk is not a constraint: a finished pack may
exceed Raster's 857 GB CERTS farm if native ortho and denser terrain
need it. We do not convert RSI tiles. Runtime RAM/VRAM on the 980 Ti
still apply. KTX2/Basis can replace the DDS later; the drape slot is
the same.

`from-geotiff` needs GDAL (`libgdal-dev`). Without GDAL the portable
`.s376dem` path and `synth-ksea` still work.

When a public ortho is weaker than the Raster pad (SKRG, VNKT), convert
Joey's own airport folder on his machine only:

```bash
./build/s376-cache import-raster /path/to/Raster/VNKT --out cache/VNKT
```

That command is read-only on the source tree. It imports georeferenced
GeoTIFF/JPEG2000/JPEG+world only. Proprietary pack files are not
decoded; the tool fails and says what it needs. Never commit the source
or `cache/`. See [docs/SOURCES.md](docs/SOURCES.md).

## osgEarth from source (Linux Mint 22 / Ubuntu 24.04)

Ubuntu 24.04 **does not** package osgEarth. The every-PR CI job therefore
builds the OSG globe + local DEM only. An optional
[osgEarth workflow](.github/workflows/osgearth.yml) (`workflow_dispatch`)
compiles osgEarth 3.7.2 from source — about 20–40 minutes, not practical
on every push.

On the hangar box, after the OSG + GDAL packages above:

```bash
sudo apt install -y libgeos-dev libproj-dev libprotobuf-dev protobuf-compiler \
    libtinyxml-dev libcurl4-openssl-dev libsqlite3-dev

git clone --depth 1 --branch osgearth-3.7.2 https://github.com/gwaldron/osgearth.git
cmake -S osgearth -B osgearth/build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++ \
    -DOSGEARTH_BUILD_TOOLS=OFF \
    -DOSGEARTH_BUILD_EXAMPLES=OFF \
    -DOSGEARTH_BUILD_IMGUI_NODEKIT=OFF \
    -DOSGEARTH_ENABLE_FASTDXT=OFF
cmake --build osgearth/build -j"$(nproc)"
sudo cmake --install osgearth/build
sudo ldconfig

# re-configure s376-visual so it defines S376_HAS_OSGEARTH
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j"$(nproc)"
```

`s376-doctor` WARNs when OSG is present but osgEarth is not. After a
source install it should PASS the osgearth check.

`data/earth/s376.earth` points at Esri World Imagery for a first look.
Replace the image/elevation layers with local NAIP / Sentinel-2 /
Copernicus GLO-30 — see [docs/SOURCES.md](docs/SOURCES.md).

Apt OSG 3.6.5 is enough for osgEarth 3.7.x. Building OSG from source
with `-DOPENGL_PROFILE=GL3` is only needed if you want a Core profile;
the hangar NVIDIA box runs the compatibility profile that apt OSG uses.

## What is in the box

- Clean-room xT4 decoder (eyepoint, TOD, visibility, fog, light steps, lights)
- HAT / HOT reply sampled from the local DEM (host-compatible 64-byte BE IEEE)
- Channel TOML: 4 projector PCs / Raster 3 frusta (far_left −50°,
  mid_left 0°, mid_right 0° duplicate front, far_right −35.25°, 50° × 32.5°)
- One process per PC via `--channel`; laptop tiles all four. `reply_master`
  + `sync_to_host` keep the four boxes on the same host pose
- Logging, `--debug`, `--doctor`, `--diagnostics`
- `s376-synth` kinematic takeoff from KSEA 16L
- Globe + local DEM drape (BC1), camera from the host, fog, sun from TOD
- `s376-cache` offline tile packer (synth, fetch, or GDAL GeoTIFF)
- `s376-cache osm ICAO` Overpass taxi/apron pull (ODbL, stays in `cache/`)

KSEA, KSFO, KLAS, KLAX, KJFK, KEWR, and KBOS are in the scene. KJFK and
KEWR use FAA both-end coordinates (NASR / AIP, source + cycle in the
airport files); the accuracy suite fails CI if an end is > 3 m off or
ALS/PAPI is wrong. See [docs/ACCURACY.md](docs/ACCURACY.md). Markings
follow FAA AC 150/5340-1; intensity follows packet-112 steps 0–5.
Taxi/apron/building graphs start schematic; `s376-cache osm ICAO` pulls
Overpass `aeroway=*` and `building=*`. Quality presets: `gtx980ti`
(default, 60 Hz × 1 on 6 GB Maxwell) and `rtx`.

Not yet: NLIM, landing-light beams, remaining airports. The plan is in
[docs/AIRPORTS.md](docs/AIRPORTS.md) and [docs/DESIGN.md](docs/DESIGN.md).
Protocol field layouts: [docs/PROTOCOL.md](docs/PROTOCOL.md).
Loopback host merge contract (ports, `HostState`, HAT/collision):
[docs/HOST_INTERFACE.md](docs/HOST_INTERFACE.md).
Parity vs Raster XT4: [docs/PARITY.md](docs/PARITY.md). Four-monitor desk:
`config/desk-4monitor.toml`.

## License

MIT. See [LICENSE](LICENSE).
