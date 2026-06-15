# vibewall

Native C++23 wallpaper picker/daemon for Wayland. This repo currently carries
the Asura XS15 `vibewallREzero` implementation, packaged as the `vibewall`
command-line workflow.

> [!WARNING]
> This project is mostly vibecoded, young, and absolutely breakable. It can
> launch shell tools, change wallpapers, kill `mpvpaper`, write SQLite/cache
> state, and talk to Wallhaven. Use it at your own risk, read the code first,
> and keep a terminal open when testing new builds.

## Inspiration

- `skwd-wall`: workflow/reference behavior for slice, grid, and hex picker
  modes.
- Noctalia: active shell IPC target for static wallpaper apply.
- `mpvpaper`: video wallpaper backend.
- Wallhaven: remote search/download/apply flow.

## Tech Stack

| Layer | Packages / APIs |
|---|---|
| Language | C++23 |
| Build | Meson, Ninja, pkg-config |
| Wayland UI | `wayland-client`, `wlr-layer-shell`, `wayland-egl`, `xkbcommon` |
| Rendering | EGL, OpenGL ES 2 |
| Images | libvips |
| Video thumbnails | ffmpeg |
| Data | SQLite |
| HTTP / JSON | libcurl, nlohmann-json |
| Config | toml++ |
| Wallpaper backends | Noctalia IPC, `mpvpaper`, `matugen` |
| Nix | Nix package plus NixOS module |

## Features

- Tiny daemon plus short-lived native picker.
- Native transparent Wayland layer-shell overlay with OpenGL ES rendering.
- Reference-inspired modes: slice carousel, grid, hex, mosaic, and Wallhaven browser.
- Systemd-backed `vibewall toggle` startup, so the first `SUPER+W` press opens
  the picker instead of only waking the daemon.
- Click outside the centered picker stage to close.
- Mouse wheel / touchpad scrolls in mode-aware steps: one item in slice mode,
  visible row chunks in grid/mosaic/hex.
- Full-opacity wallpaper previews with aspect-ratio cover cropping.
- Transparent background: the active workspace/app stays visible behind the
  centered picker; only toolbar/cards draw translucent panels.
- Per-card `STAR` / `FAV` chip toggles a local wallpaper favorite without
  applying it. The `FAV` toolbar chip filters favorites only.
- SQLite wallpaper database with tags, favorites, filters, color groups, and
  last-used restore state.
- Image thumbnails through libvips and video thumbnails through ffmpeg.
- Wallhaven paginated search/cache/download/apply.
- Wallhaven API and preview downloads run on a background worker, so WEB
  filtering and loading more pages do not block the Wayland picker event loop.
- Wallhaven card click selects only; `DOWNLOAD`/`D` saves the remote image, and `APPLY`/`Enter` downloads then applies it.
- Image backend: `noctalia msg wallpaper-set`.
- Video backend: `mpvpaper`.
- Live video apply stops `hyprpaper.service`, `hyprpaper`, and the Nix
  wrapper-style `.hyprpaper-wrapp` process before starting `mpvpaper`.
- Theme hook: `matugen image`.

## Non-Goals

No Qt, QML, Quickshell, GTK, Tauri, Electron, WebKit, Steam Workshop,
Wallpaper Engine scene support, or local AI tagging. The daemon stays IPC-only;
heavy rendering/indexing happens in short-lived tools.

## Build

```bash
meson setup build
meson compile -C build
meson test -C build
```

## Commands

```bash
vibewall scan
vibewall toggle
vibewall picker --mode slice
vibewall picker --mode grid
vibewall picker --mode hex
vibewall picker --mode mosaic
vibewall picker --wallhaven
vibewall apply /path/to/wallpaper.png
vibewall random
vibewall restore
vibewall wallhaven search "city night" --page 1
```

## Picker Keys

| Key | Action |
|---|---|
| `1` | Slice mode |
| `2` | Grid mode |
| `3` | Hex mode |
| `4` | Mosaic mode |
| `Left/Right/Up/Down` | Navigate |
| Mouse wheel / touchpad | Navigate by mode-aware scroll step |
| `Enter` | Apply selected; Wallhaven downloads then applies |
| `D` | Download selected Wallhaven wallpaper without applying |
| `F` | Toggle selected wallpaper favorite |
| `STAR` / `FAV` card chip | Toggle that card favorite without applying |
| `FAV` toolbar chip | Show favorites only |
| `W` | Search/cache Wallhaven and show remote previews |
| `L` | Return to local wallpapers |
| `R` | Apply random wallpaper |
| `/` | Edit search |
| `Backspace` | Delete search char |
| `Escape` / outside click | Close |

## Screenshots

| Slice | Grid |
|---|---|
| ![Slice picker](screenshots/vibewallrezero-slice.png) | ![Grid picker](screenshots/vibewallrezero-grid.png) |

| Hex | what now! gotcha|
|---|---|
| ![Hex picker](screenshots/vibewallrezero-hex.png) | ![Video wallpaper applied](screenshots/vibewallrezero-video-applied.png) |

| Mosaic | Wallhaven |
|---|---|
| ![Mosaic picker](screenshots/vibewallrezero-mosaic.png) | ![Wallhaven browser](screenshots/vibewallrezero-wallhaven.png) |

| Transparent overlay |
|---|
| ![Transparent overlay](screenshots/vibewallrezero-transparent-overlay.png) |

| Favorite chips |
|---|
| ![Favorite chip proof](screenshots/vibewallrezero-favorite-proof.png) |

## NixOS

The module at `nix/module.nix` installs the package, enables the user daemon,
and exposes a `programs.vibewallREzero` option set.

In this NixOS repo it lives at:

```text
/etc/nixos/asura-xs15/vibewallREzero
```

## Performance

The daemon intentionally does not link Wayland/EGL/OpenGL/libvips/curl UI
paths. Heavy indexing, thumbnailing, Wallhaven, and rendering happen in
short-lived processes.

Run:

```bash
benchmark.sh
```

Recent package benchmark on the Asura XS15, measured from the Nix-built package:

```text
package_store_size=1.2M
cache_size=16M
data_size=104K
config_size=8.0K
daemon_rss_kb=5220
daemon_cpu_percent=0.0
picker_startup_ms=1486
picker_ready_rss_kb=250440
picker_ready_cpu_percent=108.0
opened_app_rss_kb=250440
mpvpaper_rss_kb=97584
mpvpaper_cpu_percent=6.7
hyprpaper_rss_kb=0
hyprpaper_cpu_percent=0.0
noctalia_rss_kb=87272
noctalia_cpu_percent=3.8
picker_idle_cpu_ticks_10s=0
idle_redraw_policy=event-driven
```

Notes:

- The long-running daemon stays around 6 MiB RSS and does not render/decode UI
  assets.
- The picker is short-lived. Its ready-state RSS is higher because preview
  thumbnails are decoded into OpenGL textures, then released when the picker
  closes.
- The WEB view shows the newest 120 cached remote previews at once and keeps
  loading pages asynchronously. While a page is loading, additional WEB filter
  actions report `WEB STILL LOADING` instead of launching overlapping requests.
- Picker movement and scrolling repaint from memory instead of re-querying
  SQLite on every frame; data is refreshed only when filters, favorites, local
  mode, or Wallhaven results change.
- OpenGL thumbnails are capped with an LRU cache and deleted on picker shutdown
  so long WEB/grid sessions do not keep growing texture memory.
- Wallhaven worker completion is synchronized with the UI thread, SFW/NSFW
  toggles force a fresh WEB request, and daemon IPC clients have short socket
  timeouts so one stuck client cannot stall the daemon.
- The daemon also reaps picker child exits from `SIGCHLD`, so a closed picker
  does not remain as a zombie until the next IPC command.
- Video wallpapers are intentionally external: `mpvpaper` used about 98 MiB RSS
  and measurable CPU with the current active video, while `hyprpaper` was not
  running.
- `opened_app_rss_kb` is the picker while open. It should disappear after the
  picker closes; only the daemon is designed to remain resident.
- Cache storage is thumbnail/Wallhaven preview data under
  `~/.cache/vibewallREzero`; SQLite state lives under
  `~/.local/share/vibewallREzero`.

## Safety Notes

- Paths are passed as argv vectors, not shell-concatenated command strings.
- Wallhaven API keys belong in local config only; do not commit real keys.
- The picker is a privileged user-session tool in practice: it can apply
  wallpapers and spawn configured hooks, so review `config/default.toml` before
  using third-party configs.
