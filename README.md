# BloodRush, an unofficial Rush of Blood for shadPS4.

Features: 4K/PCVR(200%)/Quest(Not yet, native working but not optimized)

[![Watch on YouTube](https://img.youtube.com/vi/rQAwTf139lM/maxresdefault.jpg)](https://youtu.be/rQAwTf139lM)

# Rush of Blood — shadPS4 2D desktop changes

Changed sources from shadPS4(https://github.com/shadps4-emu/shadPS4). Use these files or `rob-2d.patch` on top of a matching upstream checkout.

## Support

If you enjoy this work, you can buy me a coffee: **https://ko-fi.com/retrodepth**

## Thanks

Huge shout-out to **detective_yoshi**

## Go to Releases to find the latest version

Upload **`release/BloodRush-Windows.zip`** as the GitHub Release asset (built by `pack-release.bat`). The zip has **BloodRush.exe** (GUI launcher), `BloodRush.bat`, 2D + PC VR builds, and optional Quest APK / PkgTool; it does **not** include the game — users put `CUSA02350` in `games\`.

## What’s in here

| Path | Role |
|------|------|
| `src/` | Full copies of the files we changed or added |
| `rob-2d.patch` | The same delta as a unified diff against `src/` |
| `BASE_COMMIT.txt` | Upstream commit this patch was based on |
| `LICENSE.md` | GPL-2.0-or-later (same as shadPS4) |
| `sync-to-github.bat` | Refresh `src/` + patch from the local `../rob2d` tree |
| `pack-release.bat` | Build `release/BloodRush-Windows.zip` (PC launch package) |
| `release-template/` | Launch bats + README copied into that zip |

New files: `src/input/desktop_play.cpp` and `src/input/desktop_play.h`.  
Touched files: `sdl_window.cpp`, `core/vr/spectator_view.h`, `video_core/renderer_vulkan/vk_presenter.cpp`, `video_core/renderer_vulkan/vk_rasterizer.cpp`, `video_core/texture_cache/texture_cache.cpp`.

Desktop 2D play is off unless its env vars are set (see the patch / sources).

## How to apply

1. Check out shadPS4 (or your fork) near the commit in `BASE_COMMIT.txt`.
2. From the emulator root, apply the patch onto `src/`:

   ```bash
   git apply --directory=src path/to/rob-2d.patch
   ```

   Or copy the files under `src/` from this repo into your tree’s `src/`.

3. Add `src/input/desktop_play.cpp` and `src/input/desktop_play.h` to the build next to the other `src/input/` sources in `CMakeLists.txt`, then rebuild.

## Credits

Built on [shadPS4](https://github.com/shadps4-emu/shadPS4). Licensed under **GPL-2.0-or-later** — see [LICENSE.md](LICENSE.md). This repo does not include game dumps or a complete emulator.
