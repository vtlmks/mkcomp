# mkcomp

A small X11 compositor with shadows, rounded corners, blur, and per-window rules. Single C99 file, no build system: just a shell script.

## Features

- **Shadows**: configurable radius, opacity, and offset
- **Rounded corners**: per-window or global
- **Background blur**: dual kawase blur behind transparent windows
- **Window opacity**: via `_NET_WM_WINDOW_OPACITY` or per-window rules
- **Focus border**: colored outline on the active window
- **Urgent border**: distinct color for `_NET_WM_STATE_DEMANDS_ATTENTION`
- **Inactive dimming**: configurable brightness reduction with animated transitions
- **Fade in/out**: smooth map/unmap animations
- **Per-window rules**: match by `WM_CLASS` to override any effect
- **Fullscreen bypass**: unredirects fullscreen windows for direct scanout
- **Vblank-driven rendering**: uses the X Present extension for proper frame pacing
- **Event-driven idle**: zero CPU usage when nothing changes
- **Hot-reload**: config changes apply immediately via inotify

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

Copy the example config and edit to taste:

```
mkdir -p ~/.config/mkcomp
cp mkcomp.conf ~/.config/mkcomp/
```

mkcomp works without a config file (sensible defaults), but most users will want to tweak at least shadows and corner radius. Changes are picked up automatically via inotify, or send `SIGHUP` to reload.

To stop, send `SIGINT` or `SIGTERM`.

## Configuration

Create `~/.config/mkcomp/mkcomp.conf` with key-value pairs. Lines starting with `#` are comments.

### Background

| Key | Default | Description |
|-----|---------|-------------|
| `bg_shader` | `noise` | Background shader (`noise` or `warp`) |
| `bg_color` | `1.0 1.0 1.0` | Primary background color (R G B, 0.0-1.0) |
| `bg_color2` | `0.2 0.1 0.05` | Secondary color for warp mode (R G B, 0.0-1.0) |
| `bg_intensity` | `0` | Shader intensity (0 = use wallpaper) |
| `bg_speed` | `1.0` | Animation speed (0 = static) |

Two animated background shaders are available:

- `noise`: perlin noise, tinted by `bg_color`
- `warp`: domain-warped FBM with rotated octaves, blending between `bg_color` (bright strands) and `bg_color2` (dark base)

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
| `blur_spread` | `1.0` | Blur kernel spread multiplier (0.5-10.0, higher = wider blur per pass) |
| `blur_desaturate` | `0` | Desaturate blurred background (0.0 = full color, 1.0 = grayscale) |
| `blur_darken` | `0` | Darken blurred background (0.0 = original brightness, 1.0 = black) |

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
| `border` | `on`, `off` | Enable/disable focus/urgent border |
| `corner_radius` | pixels | Corner radius override |
| `blur` | `on`, `off` | Enable/disable background blur |

### Example config

```
# background
bg_shader = warp
bg_color = 0.4 0.6 1.0
bg_color2 = 0.15 0.08 0.04
bg_intensity = 0.25
bg_speed = 1.0

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
blur_desaturate = 0.3
blur_darken = 0.2

# rules
rule = class:Alacritty blur=on
rule = class:polybar corner_radius=0 shadow=off
rule = class:firefox shadow=off blur=off border=off
```

## Blur

mkcomp uses dual kawase blur, which is fast and scales well to large blur radii. Blur activates automatically for windows with:

- Native transparency (32-bit ARGB windows, e.g. terminals with transparent backgrounds)
- Compositor-set opacity (via rules or `_NET_WM_WINDOW_OPACITY`)

For the best result with terminals, use the terminal's own background transparency setting rather than compositor opacity. This keeps text crisp at full brightness while only the background is transparent and blurred.

Blur can be controlled per-window with `blur=on` or `blur=off` in rules.

### Tuning the blur

Four settings control the look of the blurred background seen through transparent windows:

- `blur_strength` (0-5): how many dual-kawase passes to run. Each pass roughly doubles the effective radius, so 1 is a light frosting and 5 is heavy bokeh. 0 disables blur entirely.
- `blur_spread` (0.5-10.0): per-pass kernel offset multiplier. Higher values widen each pass without adding more passes, which is cheaper than raising `blur_strength` but introduces more banding at extreme values. Use this to reach a larger radius when `blur_strength` alone is not enough.
- `blur_desaturate` (0.0-1.0): pulls the blurred pixels toward grayscale. 0 keeps the original colors of whatever is behind the window, 1 turns the background fully monochrome. Useful when the wallpaper or window underneath is colorful enough to fight with the foreground text.
- `blur_darken` (0.0-1.0): multiplies the blurred result toward black. 0 preserves the original brightness, 1 produces a solid black tint. Combined with a mildly transparent window this gives a "smoked glass" look and improves contrast for light-on-dark UIs.

`blur_desaturate` and `blur_darken` are applied after the blur passes, so they are essentially free and can be tweaked live via the config hot-reload.

## GNOME/GTK applications

Firefox and other GTK applications detect they are running under a compositor and render their own client-side shadows and borders. This is designed for GNOME's Mutter compositor and causes problems on every other compositor: double shadows, thick borders around windows, and other visual garbage. Disable compositor effects for these windows with rules:

```
rule = class:firefox shadow=off blur=off border=off
```

Use `xprop | grep WM_CLASS` and click the offending window to find its class name.

## How it works

mkcomp uses X Composite to redirect all windows into offscreen pixmaps, binds them as OpenGL textures via `GLX_EXT_texture_from_pixmap`, and composites them with shadows, borders, rounded corners, and blur using GLSL shaders.

Rendering is event-driven: mkcomp only draws frames when something changes (window damage, focus change, config reload). When the animated background is disabled and no transitions are active, CPU usage drops to zero.

Frame pacing uses the X Present extension to synchronize rendering with the display's actual refresh rate, falling back to GLX vsync if Present is unavailable.

## License

MIT
