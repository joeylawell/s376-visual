# s376-wizard (operator console)

Raster's operator program ("Raster Wizard") is a dated channel box:
geometry fields, test patterns, and little live health. The owner
called it terrible. **s376-wizard** is the replacement. It runs on the
Host IO / operator PC and talks to the channel IGs over a **local
control channel** — not xT4, not UDP 2300/2301, and never the
simulator LAN from tests or CI.

## What we kept, what we dropped

Public / operator knowledge of Raster Wizard (no vendor files):

| Raster Wizard | s376-wizard |
|---|---|
| Per-channel heading / FOV | Same, plus flip |
| Warp / edge blend / colour | 7×5 warp, blend px, gamma, saved to TOML |
| Test patterns | `none` / `grid5` / `white70` / `black` |
| Opaque version string | Info tab: `git describe` + build date |
| No live health | FPS, GPU temp/VRAM, RAM, host-packet age, errors |
| Restart by hand | **Refresh channels** closes and reopens every IG |
| No logs | Live tail from the IG control port (or `logs/`) |

The control protocol is ours (`S376C1` key=value UDP). It is not an
xT4 packet and must not be invented as one.

## Layout

```
s376-wizard  (Host IO / operator PC)
     │  UDP S376C1  127.0.0.1:2376…  (safety-checked)
     ▼
s376-visual --channel front|left|right --control 127.0.0.1:2376
```

Default bind is loopback. `10.1.95.0/24` and `192.168.173.0/24` are
refused unless `allow_sim_lan` is set on a production box. Tests never
set that flag.

## Tabs

- **Channels** — status per IG (fps, GPU °C, VRAM, RAM, last host
  packet age, errors). **Refresh channels** sends `op=refresh` (calls
  `refresh_channels()`, which clears the Classic NYC skyline egg and
  missiles), then SIGTERM / wait / relaunch.
- **Display** — heading, FOV, flip, `resolution = WxH` (default
  1920×1200, 16:10), 7×5 warp, edge blend, gamma, test pattern.
  **Apply** is live; **Save** writes the correction TOML and patches
  the IG config `[[channels]]` block.
- **Info** — build number (`git describe` + date), GPU/driver,
  airport packs. Click the build line **7 times within 15 s** to
  unlock Classic and Missiles (same Android-style toasts as the IG
  HUD). After unlock: Current / Classic radios and Missiles Off / On.
  Missiles stay off until On. PTT mapping is `[egg]` in the IG
  config (default type 112 switch 255).
- **Logs** — live tail from each IG (`op=logs`) or `logs/s376-visual.log`.

## Run

```bash
# Laptop: one IG (all channels) + wizard
./build/s376-visual --config config/channels-3.toml --demo --control 127.0.0.1:2376
./build/s376-wizard --config config/wizard.toml

# Headless (CI / no display)
./build/s376-wizard --headless --status
./build/s376-wizard --headless --refresh
```

`config/wizard-3ch.toml` is three loopback ports for three processes
on one machine. Hangar IPs belong in a gitignored `wizard-local.toml`.

Toolkit: Dear ImGui + GLFW + OpenGL 3.3 compatibility (same GL family
as the IG). Without GLFW the binary still builds and runs `--headless`.
