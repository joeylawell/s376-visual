# Loopback host ↔ renderer contract (stable)

`s376-fakehost` is merged (BingBongBot patch). It speaks this
interface on loopback only. Joey’s Mint IOS / host emulator will
match later — `HostState`, `ControlSource`, and `CockpitIoBridge`
stay the adapter surface. Do not talk to `10.1.95.0/24`,
`192.168.173.0/24`, or UDP **3340 / 3341**.

The interface already exists. This page is the merge contract.

## Ports and bind (TOML `[host]`)

| Direction | Default | Config | Code |
|---|---|---|---|
| Host → IG | UDP **`127.0.0.1:2300`** (listen default is `0.0.0.0:2300`) | `host.bind` | `HostConfig.bind` in `include/s376/config.hpp` |
| IG → host HAT | UDP **`2301`** to the query sender | `host.reply_port` / **`--reply-port`** | `HostConfig.reply_port` |
| Optional fixed reply | empty = sender | `host.reply_to` | never a sim-LAN literal |
| HAT enable | on | `host.hat_responder` | only `reply_master` (`mid_left`) sends when 4 PCs share a TOML |
| Operator console | UDP **`127.0.0.1:2376`** | `[control].bind` | **not** xT4; not 2300/2301 |

Safety: `include/s376/safety.hpp` (`check_destination` / `check_bind`).
`allow_sim_lan` / `allow_lan` stay **false** for demo / CI / fake host.
`--demo` (synth CI) and a future fake host **must** bind/send
**127.0.0.1 only**. Switching the IG to a real host later is one
explicit, approved TOML change (`host.bind` / `allow_sim_lan`) — not a
code fork.

CLI: `--bind HOST:PORT`, `--reply-port PORT`, `--reply-master NAME`,
`--no-hat`. `--reply-port` overrides `host.reply_port`. In a
multi-channel / 4-PC run only `reply_master` (`mid_left` by default)
sends type 200 (`hat_reply_enabled`).

## In-process snapshot (what the renderer draws)

`include/s376/state.hpp` → `s376::HostState`

| Need | Field | Wire type |
|---|---|---|
| Position / attitude | `eye` (`xt4::Dcs`) | **100** CSN 1, minor −1 |
| TOD / weather | `env` (`xt4::Env`) | **124** |
| Fog | `fog` (`xt4::Fog`) | **128** |
| Clouds | `clouds[3]` | **125 / 126 / 127** |
| Storms | `storms[3]` | **105** + type-100 CSN 28–30 |
| Ownship lights | `lights` (`xt4::Lights`) | **109** (beacon, strobe, landing/bright masks) |
| Airport light steps | `light_steps[256]` | **112** (switch 0–255, step 0–5) |
| Gear / flaps | `ownship` (`xt4::OwnshipConfig`) | **180** (s376 extension) |
| Display | `display` | **131** |
| Scripted dt | `script_dt` | in-process only (`--record` / `--demo kjfk`) |

Types live in `include/s376/xt4.hpp`: `Dcs`, `Env`, `Fog`, `Lights`,
`CloudLayer`, `StormCell`, `LightSwitch`, `Query`, `OwnshipConfig`.
Builders for tests / a loopback sender: `xt4::build_dcs`, `build_env`,
`build_lights`, `build_light_switch`, `build_fog`, `build_cloud`,
`build_ownship`, `build_hat_query`, `build_datagram`. Decoder:
`xt4::Decoder`. Receiver: `HostReceiver` / `HostSocket` in
`include/s376/host.hpp`, `src/host.cpp`. Shared mailbox: `SharedState`.

Wire field layout: [PROTOCOL.md](PROTOCOL.md). Attitude: type-100
heading is **negated** true heading; `Dcs::true_heading_deg()` undoes
it. Lat/lon WGS84 degrees, alt **feet MSL**.

## Gear / flaps (type **180**, s376 extension)

`HostState.ownship` (`xt4::OwnshipConfig`) — gear 0/1/2, flaps
0/1/2/5/10/15/25/30/40. Documented in [PROTOCOL.md](PROTOCOL.md).
This is **our** layout for `s376-fakehost` and a later IOS adapter,
not a CAE ICD type. Type **109** `landing_mask` is still the seven
landing-light lobes (bits 0–6). Bit 7 is the hidden missiles-egg PTT
remap — leave it alone.

## What the IG returns (HAT + collision)

Host sends type **113** whose first i32 is **200**. The IG replies
type **200** (`xt4::build_hat_response` / `HostSocket::send_hat`):

| Type-200 word | Meaning |
|---|---|
| 10 | valid (non-zero) |
| 22–23 | **HAT ft** (IEEE BE) — radio altitude / height above DEM or roof |
| 28–29 | **HOT / elev ft** — MSL of that solid |

64 bytes, big-endian words, IEEE floats. Host-side VAX recovery is
tested in `tests/test_xt4.cpp` (`parse_hat_as_host`).

Collision is **the same reply**, not a second packet:

- Sample: `HatSample` in `include/s376/terrain.hpp`
  (`hat_ft`, `elev_ft`, `building`, `intersect`).
- DEM: `sample_hat` (`src/terrain.cpp`).
- Buildings / terminals / landmarks: `sample_hat_solids` /
  `sample_session_hat` (`include/s376/landmarks.hpp`,
  `src/landmarks.cpp`). `SolidBvh` + `cache/<ICAO>/solids.bin`.
- Segment from the previous eyepoint through a volume → `intersect`
  and **HAT ≤ 0**. Host crash detection is **host-side** on that
  threshold. The IG never sends a separate crash type.

`HostReceiver::loop` (`src/host.cpp`) is the only place that samples
and sends type 200.

## How a fake host should talk (loopback)

```
127.0.0.1:<ephemeral>  --UDP 2300-->  s376-visual  (--bind 127.0.0.1:2300)
s376-visual            --UDP 2301-->  127.0.0.1:<fake-host recv>
```

One datagram per iteration (often padded to 300 words). Minimum for
`--demo kjfk` parity: type **100** eyepoint + type **113/200** query
+ type **124** TOD + type **180** gear/flaps. Optional: **109**,
**112**, **125–128**.

`--demo` (no argument) is the **CI synth** to `127.0.0.1:2300`
(`s376-synth` / `run_synth_sender`). **`--demo kjfk`** starts
`fakehost::run_approach_sender` on loopback (six-mile-final, 6 nm
default; `--start 6sm` / `--no-six-mile-final` override) so type-200
HAT replies are exercised. `--record --demo kjfk` stays in-process
`ApproachProfile` so hangar videos do not fight 2300/2301.

A later Mint IOS emulator maps onto the same `HostState` /
`ControlSource` — one approved TOML change (`host.bind` /
`allow_sim_lan`), not a code fork.
