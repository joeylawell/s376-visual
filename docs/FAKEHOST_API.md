# s376-fakehost loopback JSON API and Windows inputs

`s376-fakehost` is a **loopback-only** stand-in host. This page is the
adapter contract: JSON-over-HTTP on the IOS port, plus the USB yoke /
throttle / pedals map. The hard guard is unchanged —
`fakehost::guard()` still accepts only 127.0.0.0/8, `::1`,
`::ffff:127.x` and `localhost`. `S376_ALLOW_LAN` /
`S376_ALLOW_SIM_LAN` do **not** relax it. A networked host mode is
**not** implemented and needs a separately approved change.

The HTML page at `GET /` and the legacy `GET /cmd?op=&arg=` query
API still work. Adapters should use `/api/v1/*`.

## Bind

```
s376-fakehost --manual --ios 127.0.0.1:8376
```

`--ios 0.0.0.0:8376` is refused. Port `0` is ephemeral; the process
prints `IOS: http://127.0.0.1:<port>/`.

Works on Windows (Winsock) and POSIX. Same binary flags on both.

## JSON-over-HTTP

All command endpoints are `POST` with `Content-Type: application/json`.
Status is `GET`. Bodies are flat JSON objects (one nesting:
`lights` on the status reply).

| Method | Path | Body | Effect |
|---|---|---|---|
| GET | `/api/v1/status` | — | live snapshot (below) |
| POST | `/api/v1/reposition` | preset **or** runway+distance **or** LLA | snap ownship |
| POST | `/api/v1/freeze` | `{"frozen":true}` optional | hold last frame |
| POST | `/api/v1/unfreeze` | — | resume |
| POST | `/api/v1/tod` | `{"hours":14.5}` | type-124 time of day |
| POST | `/api/v1/weather` | vis / wind / ceiling / rvr / precip / rwy_cond | type 124/128/125 |
| POST | `/api/v1/rwy-cond` | `{"condition":2}` (`rwy_cond` / `runway_condition`) | type-124 word 7 |
| POST | `/api/v1/lights` | step / runway / approach / environment | type-112 0–5 |
| POST | `/api/v1/cloud` | ceiling / top / upper_* | type 125 + **126** |
| POST | `/api/v1/state` | lat, lon, alt_ft, hdg, pitch, roll, gs_kt, gear, flaps | live 60 Hz feed |
| POST | `/api/v1/quit` | — | process exit |

Replies: `{"ok":true,"op":"..."}` or `{"ok":false,"error":"..."}`
with HTTP 200 / 400. `GET /api/v1/status` is the `IosStatus` object.

### Reposition

Any one of:

```json
{"preset":"kjfk-04r-6nm"}
{"icao":"KJFK","runway":"13L","distance":"6sm"}
{"icao":"KJFK","runway":"04R","on_runway":true}
{"lat":40.6399,"lon":-73.7787,"alt_ft":1500,"hdg":31}
```

`preset` is `<icao>-<rwy>-<dist|rwy>` (`6nm`, `6sm`, `10nm`, `rwy`).
`lat`/`lon`/`alt_ft`/`hdg` are **pilot-eye** WGS-84 (same as type 100;
`alt_ft` is eye MSL). Do **not** send a channel heading — the 4-channel
layout is still unconfirmed.

### Weather, TOD, clouds

```json
{"hours":19.25}
{"vis_sm":3,"wind_dir":40,"wind_mph":10,"ceiling_ft":800,"cloud_top_ft":2800,
 "rvr_ft":2400,"precip_type":0,"precip_intensity":0,"rwy_cond":2}
{"condition":2}
{"ceiling_ft":2000,"top_ft":4000,"upper_ceiling_ft":20000,"upper_top_ft":25000}
```

Upper cloud is type **126**. Lower cloud is type **125**. `<0` or omitted
clears a layer only when the field is present as a number ≤ 0.

Type-124 `runway_condition` (word 7) is `0` dry, `1` damp, `2` wet,
`≥3` flooded. Negative values clamp to 0. The visual maps those codes
to wetness 0 / 0.35 / 0.75 / 1 (`docs/PARITY.md`, `--rwy-cond` on
`s376-visual`). Also `GET /cmd?op=rwy-cond&arg=2`. The same field is
accepted on `POST /api/v1/weather`.

### Lights (type 112, steps 0–5)

```json
{"step":5}
{"runway":5,"approach":3,"environment":2}
```

| JSON field | Type-112 switches | Synth meaning |
|---|---|---|
| `runway` | 1, 2, 4 | edge, centreline, threshold/end |
| `approach` | 3 | ALS / approach bar |
| `environment` | 41, 56, 60 | taxi edge, stop bar, taxi CL |
| `step` | all of the above | default when a group is omitted |

Step 0 is off. The ownship landing-light mask (type 109) is still the
manual `l` key / `lights` button, not this endpoint.

### Live flight-state (`POST /api/v1/state`)

A local adapter may push ownship at up to 60 Hz:

```json
{"lat":40.63,"lon":-73.77,"alt_ft":1900,"hdg":31,"pitch":2.5,"roll":0,
 "gs_kt":145,"gear_down":true,"flaps":30,"timeout_s":0.25}
```

If no packet arrives within `timeout_s` (default **0.25 s**) the host
**freezes** the last frame. `POST /api/v1/unfreeze` clears the feed.
This is loopback HTTP, not a second UDP host port.

### Status object

```json
{"ok":true,"mode":"manual","preset":"kjfk-04r-6nm","phase":"manual",
 "frozen":false,"t":1.2,"lat":40.62,"lon":-73.77,"alt_ft":1900,
 "hdg":31,"pitch":2.5,"roll":0,"gs_kt":145,"wheel_agl_ft":1900,
 "gear_down":true,"flaps":30,"hat_replies":0,"last_hat_ft":0,"contacts":0,
 "tod_hours":14.5,"vis_sm":10,"wind_dir":0,"wind_mph":0,
 "ceiling_ft":-1,"cloud_top_ft":-1,"upper_ceiling_ft":-1,"upper_top_ft":-1,
 "rvr_ft":-1,"rwy_cond":0,"lights":{"step":5,"runway":5,"approach":5,"environment":5}}
```

## Windows yoke / throttle / pedals

Default backend (no extra library): **winmm** `joyGetPosEx` for USB HID
yokes (the same devices DirectInput enumerates) plus **XInput** for
Xbox-style controllers. Optional `-DS376_SDL2=ON` adds SDL2.

| Flag | Meaning |
|---|---|
| `--joystick auto` | first winmm device, else XInput 0 |
| `--joystick winmm:0` | winmm joystick id 0 (`JOYSTICKID1`) |
| `--joystick winmm:1` | second winmm device |
| `--joystick xinput:0` | XInput user 0 |
| `--joystick xinput:1` | XInput user 1 |
| `--joystick sdl:0` | SDL2 index 0 (only if `S376_SDL2`) |
| `--joystick /dev/input/eventN` | Linux evdev (unchanged) |
| `--map data/fakehost/yoke.toml` | axis / button / deadzone map |
| `--calibrate [--map-out PATH]` | sample 8 s and write a TOML map |
| `--controls-replay FILE.csv` | no-hardware input (Windows CI) |

Keyboard on Windows and Linux: `w/s` pitch, `a/d` roll, `z/x` rudder,
`r/f` throttle, `g` gear, `p` flaps, `l` lights, `q` quit.

### TOML map

`data/fakehost/yoke.toml` is the default layout. Axis `source` names:

| Name | winmm | XInput | evdev | SDL2 |
|---|---|---|---|---|
| `x` / `y` / `z` | X/Y/Z | LX/LY / RT as z | ABS_X/Y/Z | axis 0/1/2 |
| `rx` / `ry` / `rz` | R/U/V | RX / — / — | ABS_RX/Y/Z | axis 3/4/5 |
| `slider` | Z | — | — | axis 6 |
| `lx` `ly` `xinput_rx` `xinput_ry` `lt` `rt` | — | sticks / triggers | — | — |
| `hatx` `haty` | — | — | ABS_HAT0 | hat 0 |

Each `[axis.*]` has `invert`, `deadzone` (unit, default 0.04), `min`,
`max`, `center`. `[button]` indices are 0-based (XInput: 0=A, 1=B, 2=X).
`--calibrate` expands min/max/center from live motion.

## Tests

`s376-fakehost-tests` (ctest name `fakehost`) covers the guard, the
TOML map / deadzone, JSON parse + `apply_ios_command`, a live loopback
IOS server (including Windows), a 10 s `ReplayControlSource` flight,
and the existing profile / wire / in-process HAT checks. Windows CI
builds the fakehost binary and runs the same test.

## Not in this API

Channel heading configs, a non-loopback bind, and any host protocol
other than the existing xT4 subset. Do not add them here.
