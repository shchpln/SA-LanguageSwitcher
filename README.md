# SA-LanguageSwitcher

English · [Русский](README_RU.md)

**SA-LanguageSwitcher** adds language packs to the native **Options → Language** menu in GTA San Andreas 1.0 US Hoodlum. Keep English and Spanish available alongside Russian or another translation. Each pack specifies its own text file, font metrics, and textures.

This repository contains the source and an INI template. Supply the translation and fonts separately.

## How it works

- The ASI inserts rows into GTA's active menu table. The game draws the entries at its normal spacing and handles navigation.
- When you select a pack, the game loads its GXT and the plugin applies the pack's metrics and textures to both stock font slots. Selecting a stock language restores the font set that was active before switching, including fonts supplied by another mod.
- Changes take effect immediately. The plugin saves the active pack ID in the INI's `[Selection]` section and the language code in `gta_sa.set`. It restores the pack once the menu is initialized on the next launch.
- The menu can hold up to six additional languages. The number of available rows depends on the Language screen layout.

## Install

1. Put `LanguageSwitcher.asi` and `LanguageSwitcher.ini` together. With a regular ASI loader, install them in the game directory or `scripts`. With Mod Loader 0.3.10, use a dedicated folder under `modloader`. Install one copy of the ASI.
2. Set the paths to your GXT, DAT, and TXD in the INI. Each relative path is checked beside the ASI first, then in the game executable's directory. Absolute paths are also supported.
3. Start the game and select your language in **Options → Language**.

The INI template includes this Russian example:

```ini
[Language.Russian]
LanguageId=5
MenuLabel=Russian
NativeMenuLabel=Русский
TextFile=text\russian.gxt
FontMetricsFile=data\fonts_ru.dat
FontTextureFile=models\fonts_ru.txd
```

These paths expect files from the **SanLTD** or **MSLK** translation. The row reads **Russian** before activation and **Русский** once the Russian pack is active.

## Uninstall

1. With the ASI still installed and enabled, select a stock language such as **English** or **Spanish** in **Options → Language**. GTA then writes a stock language code to `gta_sa.set`.
2. Exit the game completely.
3. Remove `LanguageSwitcher.asi`. You can also remove its INI and log, plus any language resources that were installed exclusively for this mod.

If GTA fails to start after the ASI was removed, reinstall the ASI and select a stock language before removing it again. If that is unavailable, close GTA and rename the active `gta_sa.set` to `gta_sa.set.bak`. GTA will recreate its settings on the next launch. This resets game settings and controls. Save-game files are separate. The settings file usually lives in `Documents\GTA San Andreas User Files`, or in the location configured by a portable setup.

## Add another language

Create a `[Language.<ID>]` section with a unique, stable ID and point it to your pack. A Polish pack could use:

```ini
[Language.Polish]
LanguageId=7
MenuLabel=Polish
NativeMenuLabel=Polski
TextFile=text\polish.gxt
FontMetricsFile=data\fonts_pl.dat
FontTextureFile=models\fonts_pl.txd
```

| Setting | Requirements |
| --- | --- |
| `LanguageId` | Optional code exposed through the in-memory GTA language preference. Registered languages have a fixed code. Private packs can use 64–127. See [Language ID contract](LANGUAGE_IDS.md). |
| `MenuLabel` | Inactive label. Use printable ASCII so the current font can render it. |
| `NativeMenuLabel` | Active label. An empty value or unsupported glyphs fall back to `MenuLabel`. The plugin supports ASCII and the Cyrillic encoding used by the Russian pack in the template. Other alphabets do not have custom menu label encoding yet. |
| `TextFile` | A GTA SA PC GXT with 8-bit game text. The plugin checks the structure of its main and mission tables before loading. |
| `FontMetricsFile` | A DAT with `[FONT_ID]` sections 0 and 1. Each needs 208 `[PROP]` widths, `[UNPROP]`, and `[REPLACEMENT_SPACE_CHAR]`. |
| `FontTextureFile` | A TXD containing `font2` for slot 0 and `font1` for slot 1. The atlas glyph layout must match the GXT text bytes and DAT metrics. |

The active code is exposed at `CMenuManager::m_nPrefsLanguage` (address `0xBA67CC` on 1.0 US Hoodlum). Codes 0–4 belong to the stock languages. Registered codes are shared by all packs for the same language. Unregistered packs without a valid private code leave the stock code visible. The stock preference is kept as a startup fallback in the INI. Select a stock language before removing the ASI, because unpatched Hoodlum expects a code from 0 to 4 in `gta_sa.set`.

You can reorder the INI sections freely. If you rename a pack ID, select the language again in the game.

### Two translations for the same language

If `[Language.Russian]` already defines one translation, use a distinct pack ID for the second one. For example, to add SanLTD:

```ini
[Language.Russian.SanLTD]
LanguageId=5
MenuLabel=Russian (SanLTD)
TextFile=text\russian_sanltd.gxt
FontMetricsFile=data\fonts_sanltd.dat
FontTextureFile=models\fonts_sanltd.txd
```

Use the names of your own files. The GXT, DAT, and TXD must come from the same translation so that the text encoding and font layout match. After restarting, **Russian (SanLTD)** appears as a separate menu entry.

Both packs expose the Russian language code `5`. The INI stores the exact pack ID, such as `Russian.SanLTD`, so the ASI can restore the selected translation. Use an ASCII label for additional Russian sections. Cyrillic `NativeMenuLabel` encoding currently applies to `[Language.Russian]`.

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
