# mkcomp

A lightweight X11 compositor with shadows, rounded corners, blur, and per-window rules. Written in C99 as a single compilation unit with no build system -- just a shell script.

mkcomp aims to be small and correct rather than feature-complete. It does what a compositor should do (shadows, transparency, blur, focus indication) without the complexity of larger compositors.

## Features

- **Shadows** -- configurable radius, opacity, and offset
- **Rounded corners** -- per-window or global
- **Background blur** -- dual kawase blur behind transparent windows
- **Window opacity** -- via `_NET_WM_WINDOW_OPACITY` or per-window rules
- **Focus border** -- colored outline on the active window
- **Urgent border** -- distinct color for `_NET_WM_STATE_DEMANDS_ATTENTION`
- **Inactive dimming** -- configurable brightness reduction with animated transitions
- **Fade in/out** -- smooth map/unmap animations
- **Per-window rules** -- match by `WM_CLASS` to override any effect
- **Fullscreen bypass** -- unredirects fullscreen windows for direct scanout
- **Vblank-driven rendering** -- uses the X Present extension for proper frame pacing
- **Event-driven idle** -- zero CPU usage when nothing changes
- **Hot-reload** -- config changes apply immediately via inotify

## Building

Dependencies: `libX11`, `libXcomposite`, `libXdamage`, `libXfixes`, `libXpresent`, `libGL`

On Arch Linux:

```
pacman -S libx11 libxcomposite libxdamage libxfixes libxpresent mesa
```

Build:

```
sh build.sh
```

This produces a single `mkcomp` binary.

## Usage

```
mkcomp
```

mkcomp reads its configuration from `~/.config/mkcomp/config`. Send `SIGHUP` to reload the config, or just save the file (inotify picks it up automatically).

To stop, send `SIGINT` or `SIGTERM`.

## Configuration

Create `~/.config/mkcomp/config` with key-value pairs. Lines starting with `#` are comments.

### Background

| Key | Default | Description |
|-----|---------|-------------|
| `bg_color` | `1.0 1.0 1.0` | Background color (R G B, 0.0-1.0) |
| `bg_intensity` | `0.15` | Perlin noise intensity (0 = use wallpaper) |
| `bg_speed` | `1.0` | Animation speed (0 = static) |

When `bg_intensity` is 0, mkcomp shows your wallpaper by reading the root window pixmap (`_XROOTPMAP_ID`). This works with wallpaper setters like `feh`, `nitrogen`, and `hsetroot`. The wallpaper updates automatically when changed.

### Shadows

| Key | Default | Description |
|-----|---------|-------------|
| `shadow_radius` | `20` | Shadow blur radius in pixels |
| `shadow_opacity` | `0.6` | Shadow opacity (0.0-1.0) |
| `shadow_offset_x` | `5` | Horizontal offset in pixels |
| `shadow_offset_y` | `5` | Vertical offset in pixels |

### Focus border

| Key | Default | Description |
|-----|---------|-------------|
| `border_color` | `0.4 0.7 1.0` | Active window border color (R G B) |
| `border_width` | `3.0` | Border width in pixels |
| `urgent_border_color` | `0.8 0.35 0.55` | Urgent window border color (R G B) |

### Window effects

| Key | Default | Description |
|-----|---------|-------------|
| `corner_radius` | `12` | Rounded corner radius in pixels |
| `opacity` | `1.0` | Global default window opacity |
| `inactive_brightness` | `1.0` | Brightness of inactive windows (0.0 = black, 1.0 = full) |
| `focus_transition_ms` | `0` | Dim/brighten animation duration in ms |
| `fade_in_ms` | `0` | Window appear animation duration in ms |
| `fade_out_ms` | `0` | Window close animation duration in ms |
| `blur_strength` | `0` | Background blur passes (0 = off, 1-5 = weak to strong) |

### Per-window rules

Rules match windows by `WM_CLASS` (case-insensitive, matches both instance and class name). Use `xprop` to find a window's class.

```
rule = class:ClassName property=value [property=value ...]
```

Available properties:

| Property | Values | Description |
|----------|--------|-------------|
| `opacity` | 0.0-1.0 | Window opacity override |
| `shadow` | `on`, `off` | Enable/disable shadow |
| `corner_radius` | pixels | Corner radius override |
| `blur` | `on`, `off` | Enable/disable background blur |

### Example config

```
# background
bg_color = 0.4 0.6 1.0
bg_intensity = 0.25
bg_speed = 4.0

# shadows
shadow_radius = 20
shadow_opacity = 0.6
shadow_offset_x = 5
shadow_offset_y = 5

# focus border
border_color = 0.3 0.5 0.8
border_width = 1.5
urgent_border_color = 0.8 0.35 0.55
focus_transition_ms = 100

# window effects
corner_radius = 6
inactive_brightness = 0.8
blur_strength = 3

# rules
rule = class:Alacritty blur=on
rule = class:polybar corner_radius=0 shadow=off
```

## Blur

mkcomp uses dual kawase blur, which is fast and scales well to large blur radii. Blur activates automatically for windows with:

- Native transparency (32-bit ARGB windows, e.g. terminals with transparent backgrounds)
- Compositor-set opacity (via rules or `_NET_WM_WINDOW_OPACITY`)

For the best result with terminals, use the terminal's own background transparency setting rather than compositor opacity. This keeps text crisp at full brightness while only the background is transparent and blurred.

Blur can be controlled per-window with `blur=on` or `blur=off` in rules.

## How it works

mkcomp uses X Composite to redirect all windows into offscreen pixmaps, binds them as OpenGL textures via `GLX_EXT_texture_from_pixmap`, and composites them with shadows, borders, rounded corners, and blur using GLSL shaders.

Rendering is event-driven: mkcomp only draws frames when something changes (window damage, focus change, config reload). When the animated background is disabled and no transitions are active, CPU usage drops to zero.

Frame pacing uses the X Present extension to synchronize rendering with the display's actual refresh rate, falling back to GLX vsync if Present is unavailable.

## License

MIT
