# SA-LanguageSwitcher

English · [Русский](README_RU.md)

**SA-LanguageSwitcher** adds language packs to the native **Options → Language** menu in GTA San Andreas 1.0 US Hoodlum. Keep English and Spanish available alongside Russian or another translation. Each pack specifies its own text file, font metrics, and textures.

This repository contains the source and an INI template. Supply the translation and fonts separately.

## How it works

- The ASI inserts rows into GTA's active menu table. The game draws the entries at its normal spacing and handles navigation.
- When you select a pack, the game loads its GXT and the plugin applies the pack's metrics and textures to both stock font slots. Selecting a stock language restores the font set that was active before switching, including fonts supplied by another mod.
- Changes take effect immediately. The plugin saves the active pack ID in the INI's `[Selection]` section and restores the selection once the menu is initialized on the next launch.
- The menu can hold up to six additional languages. The number of available rows depends on the Language screen layout.

## Install

1. Put `LanguageSwitcher.asi` and `LanguageSwitcher.ini` together. With a regular ASI loader, install them in the game directory or `scripts`. With Mod Loader 0.3.10, use a dedicated folder under `modloader`. Install one copy of the ASI.
2. Set the paths to your GXT, DAT, and TXD in the INI. Each relative path is checked beside the ASI first, then in the game executable's directory. Absolute paths are also supported.
3. Start the game and select your language in **Options → Language**.

The INI template includes this Russian example:

```ini
[Language.Russian]
MenuLabel=Russian
NativeMenuLabel=Русский
TextFile=text\russian.gxt
FontMetricsFile=data\fonts_ru.dat
FontTextureFile=models\fonts_ru.txd
```

These paths expect files from the **SanLTD** or **MSLK** translation. The row reads **Russian** before activation and **Русский** once the Russian pack is active.

## Add another language

Create a `[Language.<ID>]` section with a unique, stable ID and point it to your pack. A Polish pack could use:

```ini
[Language.Polish]
MenuLabel=Polish
NativeMenuLabel=Polski
TextFile=text\polish.gxt
FontMetricsFile=data\fonts_pl.dat
FontTextureFile=models\fonts_pl.txd
```

| Setting | Requirements |
| --- | --- |
| `MenuLabel` | Inactive label. Use printable ASCII so the current font can render it. |
| `NativeMenuLabel` | Active label. An empty value or unsupported glyphs fall back to `MenuLabel`. The plugin supports ASCII and the Cyrillic encoding used by the Russian pack in the template. Other alphabets do not have custom menu label encoding yet. |
| `TextFile` | A GTA SA PC GXT with 8-bit game text. The plugin checks the structure of its main and mission tables before loading. |
| `FontMetricsFile` | A DAT with `[FONT_ID]` sections 0 and 1. Each needs 208 `[PROP]` widths, `[UNPROP]`, and `[REPLACEMENT_SPACE_CHAR]`. |
| `FontTextureFile` | A TXD containing `font2` for slot 0 and `font1` for slot 1. The atlas glyph layout must match the GXT text bytes and DAT metrics. |

You can reorder the INI sections freely. If you rename a pack ID, select the language again in the game.

## Compatibility and diagnostics

The target is **GTA San Andreas 1.0 US Hoodlum (x86)**. Without Mod Loader, the ASI uses the stock menu table. With Mod Loader 0.3.10, it finds the relocated table and preserves the existing GXT handling for stock languages. It applies to GTA's text and menus. SA-MP client chat and dialogs are outside its scope.

Set `Enabled=0` under `[General]` to disable the switcher. `EnableLogging=1` creates a fresh `LanguageSwitcher.log` beside the ASI each session with the detected game version, resource errors, and hook addresses. The ASI leaves unsupported builds and incompatible hook sites unpatched.

## Build from source

Install Visual Studio 2022 Build Tools with MSVC v143 x86 support and the Windows SDK. Run these commands from the repository directory:

```powershell
git clone https://github.com/DK22Pac/plugin-sdk.git
git -C plugin-sdk checkout 15f15b60bbf74c106e1b496ff92c98764abf4605
.\build.ps1 -PluginSdk .\plugin-sdk
```

Use `-PluginSdk` to point to an existing SDK checkout. If the SDK is in a sibling folder named `plugin-sdk`, run `.\build.ps1` without arguments. The script builds `LanguageSwitcher.asi` as Win32 Release and checks its architecture and dependencies with `dumpbin`.

See [LICENSE](LICENSE) for the project license and [LICENSE.plugin-sdk.txt](LICENSE.plugin-sdk.txt) for the plugin-sdk notice.
