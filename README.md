# BloodRush, an unofficial Until Down: Rush of Blood for shadPS4.

Features: 4K/PCVR(200%)/Quest(Not yet, native working but not optimized)

Watch on Youtube:

[![Watch on YouTube](https://img.youtube.com/vi/rQAwTf139lM/maxresdefault.jpg)](https://www.youtube.com/watch?v=MtbgST3LOeU)

# Rush of Blood — shadPS4 2D desktop changes

Changed sources from shadPS4(https://github.com/shadps4-emu/shadPS4). Use these files or `rob-2d.patch` on top of a matching upstream checkout.

## Support

If you enjoy this work, you can buy me a coffee: **https://ko-fi.com/retrodepth**

## Thanks

Huge shout-out to **[Rodrigo Marcos - LAB VR](https://www.youtube.com/watch?v=MtbgST3LOeU)**, the crash fixer and motion sim god!

Huge shout-out to **detective_yoshi**

## Go to Releases to find the latest version (NO ASSETS NOR GAME FILES PROVIDED) 

## What’s in here

| Path | Role |
|------|------|
| `src/` | Full copies of the files we changed or added |
| `rob-2d.patch` | The same delta as a unified diff against `src/` |
| `BASE_COMMIT.txt` | Upstream commit this patch was based on |
| `rob2d-src/` | Complete snapshot of the 2D emulator source tree (shadPS4 plus our changes), as built by `rob2d/build2d.py`; no game files, binaries or build output |
| `rushquest-src/` | Complete snapshot of the PC VR / Quest emulator source tree (`RushQuest/shadps4-arm64-main/src`); no game files, binaries or build output |
| `LICENSE.md` | GPL-2.0-or-later (same as shadPS4) |


## Credits

Built on [shadPS4](https://github.com/shadps4-emu/shadPS4). Licensed under **GPL-2.0-or-later** — see [LICENSE.md](LICENSE.md). This repo does not include game dumps or a complete emulator.
