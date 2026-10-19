# Host visual protocol (clean-room)

Loopback merge contract for a simulated host (ports, `HostState`, HAT
reply, no new types): [HOST_INTERFACE.md](HOST_INTERFACE.md).

This is a field-layout description written from independently observed
sizes, the host-side fill order used by the S376 visual module, and the
s376bridge decoder tests. It is **not** a copy of any RSI header or ICD
text. Packet type numbers and magic values are industry-visible constants
that any interoperable IG has to match.

## Transport

- Host → IG: one UDP datagram per visual iteration, default destination
  port **2300**.
- IG → host: optional height-above-terrain (HAT) reply, default
  destination port **2301**, sent to the source address of the query.
- Operator console (`s376-wizard`) uses a **separate** loopback UDP
  channel (`S376C1`, default **2376**). That is not an xT4 packet and
  must never be sent on 2300/2301 or the simulator LAN from tests.
- A datagram is a concatenation of packets, often padded to 300 16-bit
  words (600 bytes). Zero words between packets are gaps and are skipped.
- 32-bit and 64-bit fields are stored **most-significant 16-bit word
  first**. Each 16-bit word itself may be little-endian (raw VAX memory
  copy, magic bytes `3f 2d 01 ab`) or big-endian (network order, magic
  bytes `2d 3f ab 01`). The decoder detects this per datagram.
- Floats are either VAX F (32-bit) / D (64-bit) or IEEE-754. VAX F/D on
  the wire has the sign/exponent word as the **second** 16-bit word of
  the field (the first word of VAX memory). The decoder tries both
  formats on the first plausible eyepoint and then locks.

A 20-byte preamble starts every packet:

| Bytes | Meaning |
|---|---|
| 0–3 | Magic `0x2D3FAB01` as two 16-bit words `0x2D3F`, `0xAB01` |
| 4–7 | Packet size in bytes, preamble included, high word first |
| 8–11 | Packet type, high word first |
| 12–15 | Flags. Hosts of VAX ancestry set bit 2 (`0x0004`) |
| 16–19 | Reserved, zero |

## Packet types used in v0.1

| Type | Role | Typical size |
|---|---|---|
| 100 | Eyepoint / other coordinate systems | 72 bytes |
| 109 | Ownship lights (beacon, strobe, landing-light mask) | 72 bytes |
| 112 | Runway / approach light intensity step (0–5) | 24 bytes |
| 113 | Query (type 200 asks for a HAT reply) | 32 bytes |
| 105 | Storm-cell select (one of 30; ≤3 active) | 32 bytes |
| 124 | Environment: time of day, visibility, precip, date | 74 bytes |
| 125 | Lower cloud layer | 56 bytes |
| 126 | Upper cloud layer | 56 bytes |
| 127 | Top cloud layer (on/off + catalog) | 24 bytes |
| 128 | Fog layer (region, height, RVR) | 36 bytes |
| 131 | Display control (channel on/off/standby) | 24 bytes |
| **180** | **Ownship config (s376 extension: gear / flaps)** | **24 bytes** |
| 200 | HAT / IG status reply | 64 bytes as the S376 host reads it |

Other type numbers exist on the wire (traffic, switches, …). The
decoder counts unknown types and ignores the body. Cloud and storm
layouts below are clean-room field order from the hangar reference
header (that file is never committed).

## Eyepoint (type 100)

Body, 16-bit word offsets after the preamble:

| Words | Type | Field |
|---|---|---|
| 0 | i16 | Coordinate-system number. **1** is the pilot eyepoint |
| 1 | i16 | Minor CSN. **−1** for the eyepoint |
| 2 | i16 | Model select. Eyepoint uses **1** |
| 3 | u16 | Host packet count, 1…32767 |
| 4–5 | f32 | Heading, **negated** true heading (degrees) |
| 6–7 | f32 | Pitch, nose-up positive (degrees) |
| 8–9 | f32 | Roll, right-wing-down positive (degrees) |
| 10–13 | f64 | Latitude, degrees, pilot eye |
| 14–17 | f64 | Longitude, degrees, pilot eye |
| 18–21 | f64 | Altitude, feet MSL, pilot eye |
| 22–25 | f64 | Timestamp (often unused; the IG may fill it) |

Traffic, marshaller and other movers reuse type 100 with other CSN
values and are ignored for the camera.

A decode is treated as a plausible eyepoint when latitude is in
[−90, 90], longitude in [−180, 180], altitude in [−2000, 60000] ft,
heading within ±720°, pitch within ±90°, roll within ±180°, and the
position is not (0, 0).

## Environment (type 124)

| Words | Type | Field |
|---|---|---|
| 0 | i16 | Time-of-day select. **−1** means use the continuous hour field. 0…3 are day / dusk / dawn / night steps |
| 2 | i16 | Precipitation type (0 off, 1 rain, 2 snow, 3 hail) |
| 3 | i16 | Precipitation intensity 0…10 |
| 4–6 | i16 | Month, day, year |
| 7 | i16 | Runway surface condition code |
| 10–11 | i32 | Lightning trigger |
| 12–13 | f32 | Continuous time of day, hours 0…24 |
| 14–15 | f32 | Global visibility, feet (cap 600000) |
| 18–19 | f32 | Surface wind, mph |
| 20–21 | f32 | Wind direction, degrees |

## Storm cell (type 105)

At most **3** cells are active. The host names a catalog slot 0…29.

| Words | Type | Field |
|---|---|---|
| 0 | i16 | Catalog index 0…29 |
| 1 | i16 | Non-zero = active |
| 2 | i16 | Positioning CSN. **28 / 29 / 30** are the three active cells. **−1** = use the IG catalog |
| 3 | i16 | Precipitation type (same codes as type 124) |
| 4 | i16 | Precipitation intensity 0…10 |
| 5 | i16 | Lightning. **0** off, **1** random flashes, **>1** flash on change |

Position is a type-100 DCS on that CSN (lat / lon / alt). Cell **size** is
not on this packet; the original IG used a catalog. We stand in 1.5 nm
scaled by intensity.

## Cloud layers (types 125 / 126 / 127)

Types 125 (lower) and 126 (upper) share a body. Type 127 is only
region + select; the original IG took top-layer heights from its catalog.

| Words | Type | Field |
|---|---|---|
| 0 | i16 | Region. **−1** is global |
| 1 | i16 | Catalog / type index. **−1** is off |
| 2 | i16 | Scud bits (bit 0 ceiling, bit 1 top) |
| 3 | i16 | Transition-zone flag |
| 4–5 | f32 | Ceiling (base), feet **MSL** |
| 6–7 | f32 | Top, feet **MSL** |
| 8–9 | f32 | Visibility above the layer, feet. **−1** = use global |
| 10–11 | f32 | Layer wind, knots |
| 12–13 | f32 | Layer wind direction, degrees |
| 14–15 | f32 | Lower transition, feet. **−1** = catalog |
| 16–17 | f32 | Upper transition, feet. **−1** = catalog |

Type 127 body is words 0–1 only.

## Fog layer (type 128)

| Words | Type | Field |
|---|---|---|
| 0 | i16 | Region. **−1** is global |
| 1 | i16 | Layer select. **−1** is off |
| 2 | i16 | Patchy RVR flag |
| 3 | i16 | Speed-cue intensity 0…10 |
| 4–5 | f32 | Layer height, feet AGL |
| 6–7 | f32 | Runway visual range, feet |

## Light intensity step (type 112)

| Words | Type | Field |
|---|---|---|
| 0 | i16 | Switch number, 0…255 |
| 1 | i16 | Intensity step **0 (off) … 5 (max)** |

Airport light groups (runway edge, centreline, approach, PAPI, taxi)
are mapped to switch numbers in the airport data, not in this packet.
The hidden missiles egg (off by default) can treat **switch 255** as
the cockpit PTT rising edge (`egg.ptt_source = "switch"`). That is a
config remapping of this existing packet, not a new type.

## Ownship lights (type 109)

| Words | Type | Field |
|---|---|---|
| 0 | i16 | Beacon on/off |
| 1 | i16 | Strobe on/off |
| 2 | i16 | Landing-light lobe mask (7 bits: L outboard, L turnoff, L inboard, centre, R inboard, R turnoff, R outboard) |
| 3 | i16 | Bright mask, same bits |

Bit 7 of the landing mask is unused by the seven lobes. The hidden
missiles egg can map that bit as PTT (`egg.ptt_source = "landing_bit"`,
`egg.ptt_landing_bit = 7`). Again, not a new packet.

## Query (type 113) and HAT reply (type 200)

A query whose first i32 is **200** asks for height above terrain of
coordinate system 1.

The S376 host reads a short, big-endian, IEEE reply (not VAX floats):

| 16-bit word (0-based) | Meaning |
|---|---|
| 0–1 | Magic |
| 2–3 | Size 64 |
| 4–5 | Type 200 |
| 10 | Valid flag, non-zero = good |
| 22–23 | Height above terrain, feet, IEEE big-endian |
| 28–29 | Terrain elevation, feet, IEEE big-endian |

The host then byte-swaps each 16-bit word and adds 2 to the IEEE
exponent to recover a VAX F value. `tests/test_xt4.cpp` rebuilds that
host-side conversion so a reply the IG emits is proven readable.

The IG samples the loaded DEM (`DemHeightSource` / `.s376dem` or a
synth KSEA box) at the eyepoint, then raises HOT to any CPU-side
building / landmark roof under the point (active skyline era). The
solid set is `solids.bin` (every OSM footprint) plus schematic
airport boxes plus visible-era landmarks — independent of mesh LOD,
the 8 k bake cap, and terrain paging. Height AGL is OSM `height`,
else `building:levels` × 3.5 m, else 12 m; the prism sits on DEM
ground. HAT is MSL altitude minus that solid. A segment from the
previous eyepoint through a volume is a hit: HAT is forced ≤ 0. Off
the DEM and off every solid the reply is still valid with elev = 0
and hat = altitude (the old spherical stand-in).

Host crash detection is **host-side**. The IG does not send a
separate crash packet. The host must act on this type-200 reply:

| Field | Words | How the host should treat it |
|---|---|---|
| Valid | 10 | Non-zero = use the sample |
| HAT ft | 22–23 | Radio altitude / height above solid. **≤ 0 (or below the host's own radio-alt threshold) means the ownship is in or on a building or the ground** |
| HOT / elev ft | 28–29 | MSL of that solid (DEM or roof) |

## Ownship configuration (type 180) — s376 extension

This packet is **designed in this repository** so a loopback host
(`s376-fakehost`) and Joey’s future Mint IOS emulator can tell the IG
the 737-300 landing configuration. It is **not** copied from any CAE
or RSI ICD. A later approved host can keep this layout or map onto it
in an adapter; the IG only reads these two fields.

Typical size 24 bytes (10-word preamble + 2 body words):

| Words | Type | Field |
|---|---|---|
| 0 | i16 | Gear: **0** up, **1** down, **2** in transit |
| 1 | i16 | Flaps detent: **0, 1, 2, 5, 10, 15, 25, 30, 40** (737-300) |

Builders: `xt4::build_ownship` / `decode_ownship`. The decoder ignores
unknown types, so an older IG without this packet still draws.

## Display control (type 131)

| Words | Type | Field |
|---|---|---|
| 0 | i16 | Channel index. **−1** means all |
| 1 | i16 | 0 off, 1 on, 2 standby |

## Units

- Horizontal position: geodetic degrees, WGS84.
- Altitude and visibility: feet.
- Attitudes: degrees.
- Wind: miles per hour.

Internal rendering uses metres and WGS84
(`a = 6378137 m`, `b = 6356752.3142 m`).
