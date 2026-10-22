# W3ManaBars

Mana bars under the unit health bars for **Warcraft III 1.26a, 1.27b and 1.28.5**, as in the later patches.

[Описание на русском](README.ru.md)

- Every unit with a mana pool (heroes, casters) gets a blue mana bar right under its health bar, across the same width. Units without mana get none.
- The bar is the game's own mana bar (the one it uses in the console), placed over units. It shows and hides with the health bar and follows it everywhere.
- With [W3TrueWidescreen](https://github.com/Hr0ffT/W3TrueWidescreen) the mana bars behave like the health bars at the console: drawn under it, moved beside it, hidden behind it.

Everything happens in memory while the game runs. No game files are modified.

## Requirements

- Warcraft III 1.26a (Game.dll 1.26.0.6401), 1.27b (Game.dll 1.27.1.7085) or 1.28.5 (Game.dll 1.28.5.7680). The mod checks the version and does nothing on any other patch.

## Installation

1. Copy `W3ManaBars.mix` and `W3ManaBars.ini` into the Warcraft III folder, next to `war3.exe`.
2. Start the game. The `.mix` is loaded automatically, no launcher needed.

Works alongside other `.mix` mods, for example [W3TrueWidescreen](https://github.com/Hr0ffT/W3TrueWidescreen), [W3MinimapTweaks](https://github.com/Hr0ffT/W3MinimapTweaks) and [W3MultislotQuickSave](https://github.com/Hr0ffT/W3MultislotQuickSave).

To uninstall, delete `W3ManaBars.mix` and `W3ManaBars.ini`.

## Settings

All in `W3ManaBars.ini`:

| Setting | Default | What it does |
|---|---|---|
| `Height` | 100 | Height of the mana bar in percent of the health bar's height (20..300) |
| `ManaColor` | 2F6BFF | Colour of the mana bar as `RRGGBB`. The game's own mana blue (`0000FF`) is hard to tell from the black of the empty part |
| `Debug` | 0 | 1 = write `W3ManaBars.log` next to the game (for bug reports) |

## How it works

The health bars over units are `CStatBar` frames. A `CStatBar` has a type: the small health bar over units is one of them, the small mana bar (`SimpleManaBarConsoleSmall`) another, which the game never puts over units. Every frame the game binds each shown health bar to its unit. When the unit has a mana pool, the mod makes a mana bar as a child of the health bar, anchored right under it and bound to the same unit. As a child it shows, hides and moves with the health bar on its own.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

## Building from source

```
i686-w64-mingw32-gcc -O2 -Wall -shared -static-libgcc -s -o W3ManaBars.mix src/W3ManaBars.c -lversion
```

## License

MIT, see [LICENSE](LICENSE).
