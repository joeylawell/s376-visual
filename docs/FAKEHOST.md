# s376-fakehost — offline 737-300 host stand-in

`s376-fakehost` drives `s376-visual` the way the simulator host does, with
no simulator, no LAN and no sound. It is meant for the KJFK demo flight and
for Joey's gaming PC standing in for the host.

It speaks **only** the existing clean-room xT4 subset in
[PROTOCOL.md](PROTOCOL.md), built with the same `xt4::build_*` helpers as
`s376-synth`: type 100 eyepoint, 113 HAT query, 109 ownship lights,
124 environment (TOD, visibility, wind, date), 125/127 cloud, 128 fog,
112 light steps, 131 display on, and type **180** ownship gear/flaps
(an s376-visual extension, not a CAE ICD type). It reads the type-200
HAT reply on 2301. No ICD or manual text.

## Safety: loopback only

`fakehost::guard()` accepts 127.0.0.0/8, `::1`, `::ffff:127.x` and
`localhost`, nothing else. It runs on every `--dest`, `--listen`, `--ios`
and `--cockpit-io` spec at parse time, on the resolved `sockaddr` before
`bind()`, before **every** `sendto()`, and on the source of every reply
(non-loopback replies are dropped). Wildcard binds (0.0.0.0, ::) are
refused. 10.1.95.0/24, 192.168.173.0/24 and ports 3340/3341 get explicit
refusal messages. `S376_ALLOW_LAN` / `S376_ALLOW_SIM_LAN` do **not** relax
it, and there is no flag that does. Exit code 4 = refused.

## Quick start (one box)

```sh
# renderer (headless works with no GPU; drop --headless for the window)
s376-visual --bind 127.0.0.1:2300 --reply-port 2301 --no-control   # [--headless]
# host: 6 nm final 04R, real-time 60 Hz, HAT checks
s376-fakehost --profile kjfk-04r --fps 60 --check
# or one process: --demo kjfk starts fakehost on loopback (six-mile-final, 6 nm)
s376-visual --demo kjfk --six-mile-final --start 6nm
```

The renderer replies HAT to the sender's IP on `host.reply_port` (2301),
which is the fakehost's default `--listen 127.0.0.1:2301`. Keep
`reply_master` empty or matching the selected `--channel`, and do not pass
`--no-hat`, or the fakehost sees no replies (`--check` then exits 3).

## Modes

| Mode | Command |
|---|---|
| Scripted approach | `--profile kjfk-04r` / `kjfk-13l` / any `<icao>-<rwy>` in `faa.cpp` |
| IOS "6 mile final" | `--profile kjfk-04r --six-mile-final [--start 6sm]` |
| Replay | `--replay data/fakehost/kjfk-04r-sample.csv` (or `.toml`) |
| Manual | `--manual [--joystick winmm:0\|xinput:0\|/dev/input/eventN] [--map yoke.toml]` |

`--start` takes units: `6nm` (default), `6sm`, `10nm`, `11km`, bare number = nm.

### Approach profile (737-300)

* Threshold lat/lon/elevation and true bearing come from the FAA NASR / AIP
  rows in `src/faa.cpp` (`faa_kjfk()`); displaced thresholds are applied
  (13L: 907 ft). 04R: 40.6254283 N, 73.7703456 W, 11.9 ft, 031 T.
* 3° path to a 1000 ft aim point: TCH 52.4 ft; 1963 ft AGL at 6 nm,
  1713 ft at 6 sm, 3237 ft at 10 nm.
* Default (not `--six-mile-final`): flaps 15, gear up, Vref+25; gear down
  at 5 nm, flaps 30 at 4 nm, decelerate to Vref+5 (145 kt), stabilized by
  1000 ft. `--six-mile-final`: gear down, flaps 30, 145 kt from frame 1.
* Pitch = alpha + flight-path angle (~2.5° on the path); a 30 ft S-turn
  with consistent ±1.3° bank above 1000 ft; wings level below 300 ft.
* Flare at 30 ft (exponential, ~120 fpm at touchdown, ~5.5° pitch),
  touchdown ~1700 ft past the threshold, derotate, decelerate to 20 kt taxi,
  hold 5 s. Landing lights (all 7 lobes) on until taxi speed, then turnoff
  lights only; strobes on; beacon on.
* Gear and flaps go out as type **180** (`xt4::build_ownship`) every
  frame, and are also logged / written to the trajectory file.

### Reposition presets

The repo and its docs define **no** host reposition preset (searched
docs/, HANDOFF.md, README.md, config/, src/). The "6" preset is therefore
modelled as an IOS-style "6 mile final": on centreline, on the 3° path,
landing configured, Vref+5. Default distance is 6 nm; `--start 6sm` gives
the statute-mile variant. The IOS page offers both.

### Trajectory files

CSV header `t,lat,lon,alt_ft,hdg,pitch,roll,gs_kt,gear,flaps,landing_mask,strobe`
(the first seven are required; `#` comment lines allowed). `alt_ft` is the
pilot eye MSL; `hdg` true. TOML: repeated `[[point]]` tables with the same
keys. Timestamps must increase; replay interpolates (heading wraps).
`--record-out f.csv` records any mode for later replay.

### Manual mode / inputs (pluggable)

`fakehost::ControlSource` is the input interface; sources stack:

* keyboard (Windows and Linux): w/s pitch, a/d roll, z/x rudder, r/f throttle,
  g gear, p flaps, l lights, q quit
* Linux evdev USB yoke / throttle / pedals: `--joystick /dev/input/eventN`
* Windows winmm `joyGetPosEx` + XInput: `--joystick auto` / `winmm:0` /
  `xinput:0` (USB HID yokes show up as winmm devices)
* Optional SDL2: build `-DS376_SDL2=ON`, then `--joystick sdl:0`
* Axis/button map + deadzone/calibration: `--map data/fakehost/yoke.toml`,
  `--calibrate --map-out calibrated.toml`
* `--controls-replay data/fakehost/controls-10s.csv` — no-hardware CI input
* `CockpitIoBridge` — **stub interface only** (`--cockpit-io 127.0.0.1:PORT`),
  for a future local cockpit-IO daemon. No CAE or vendor protocol.

Mapping, Windows flag names and the JSON API: [FAKEHOST_API.md](FAKEHOST_API.md).

Flight model: attitude-hold pitch (stick = pitch rate), lift ∝ v²·α with
bank load factor, drag by flaps/gear, so it is speed-stable and trimmed
hands-off at 145 kt / 3° / throttle 0.45 / flaps 30 / gear down; gentle
wings-level tendency; ground roll with brakes at idle.

### IOS (loopback web page + JSON API)

`--ios 127.0.0.1:8376` then open http://127.0.0.1:8376/ (Windows Winsock
or POSIX). Reposition (preset, runway+distance, or lat/lon/alt/hdg),
freeze/unfreeze, TOD, weather, runway condition (type-124 word 7:
0 dry / 1 damp / 2 wet / ≥3 flooded), lights, upper cloud, and a 60 Hz
live state feed. Adapter contract: [FAKEHOST_API.md](FAKEHOST_API.md).

```sh
curl 'http://127.0.0.1:8376/cmd?op=reposition&arg=kjfk-13l-6sm'
curl -s http://127.0.0.1:8376/api/v1/status
curl -s -X POST http://127.0.0.1:8376/api/v1/tod -d '{"hours":19.5}'
curl -s -X POST http://127.0.0.1:8376/api/v1/rwy-cond -d '{"condition":2}'
curl -s -X POST http://127.0.0.1:8376/api/v1/reposition \
  -d '{"lat":40.64,"lon":-73.78,"alt_ft":1500,"hdg":31}'
```

### N channels

Repeat `--dest` (all loopback): one datagram is sent to each.
Run each renderer on its own port, e.g.

```sh
s376-visual --channel far_left  --bind 127.0.0.1:2300 --no-control --reply-master mid_left &
s376-visual --channel mid_left  --bind 127.0.0.1:2310 --no-control --reply-master mid_left &
s376-fakehost --profile kjfk-04r --dest 127.0.0.1:2300 --dest 127.0.0.1:2310
```

Only one channel should answer HAT (`--reply-master`), since all replies
go to 2301.

### HAT / collision checks

Every frame carries a type-113 HAT query. Each type-200 reply is logged
against the frame just sent: `TOUCHDOWN` (reply HAT minus 14 ft eye height
must be within `--hat-tol`, default 20 ft) and `CONTACT` (valid reply with
HAT ≤ 0 = eye in/on terrain or a structure, per PROTOCOL.md). Without a
DEM cache the IG reports elev 0 and HAT = MSL altitude, so touchdown shows
"wheel 11.9 ft" (the KJFK field elevation); with the KJFK cache it should
read ≈ 0.

## Future real-network mode (not implemented)

Driving a renderer on another PC (e.g. the gaming PC to the projector PCs)
would need an **explicit, separately approved** change: a distinct build
option (e.g. `-DS376_FAKEHOST_NET=ON`, default OFF) plus a run-time flag
naming the exact allowed peer list, a refusal that still holds for
10.1.95.0/24, 192.168.173.0/24 and 3340/3341 unless a second sim-LAN
approval is given, a startup banner and log line, and tests for both
modes. Until Joey approves that, the guard stays loopback-only.

## Tests

`s376-fakehost-tests [path/to/s376-visual]` (ctest name `fakehost`,
built on Windows too): guard, distance units, glidepath/TCH and full
04R/13L profiles, replay CSV/TOML round-trip, yoke TOML / deadzone,
JSON IOS parse + apply, live loopback HTTP (Winsock or POSIX), 10 s
manual-input replay, wire decode in all 4 encodings, flight-model trim,
in-process end-to-end through the renderer's own `HostReceiver`, and,
with a path, the real `s376-visual --headless` binary on
127.0.0.1:2300/2301 (POSIX).
