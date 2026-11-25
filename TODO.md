# TODO

## Support more GTA executables

- [ ] Add support for other GTA San Andreas PC executables. Start by checking 1.0 US Compact and 1.0 EU.
- [ ] Record the exact executable revision and verified hook signatures for each build. Cover the menu table, GXT loading, settings, font rendering, and the SCM language opcode. If a build or hook cannot be validated, leave it unpatched and report the reason in the log.
- [ ] Retest startup, language switching, settings restoration, and scripted shop menus with and without Mod Loader for every supported build.

## Support Windows system fonts

- [ ] Allow a language pack to select an installed Windows font family in place of its pre-rendered TXD font textures. Retain DAT/TXD packs as a supported option.
- [ ] Convert GXT text bytes to Unicode using the pack's glyph mapping before rendering system fonts. Use consistent glyph measurements for line wrapping, alignment, and menu layout. Preserve both GTA font slots and the styles that use them.
- [ ] Define fallbacks for a missing font family or glyph, and check rendering at different resolutions and after a Direct3D device reset.

## Extend translation support to other mods

- [ ] Define how a mod registers its base text resource and translations. Support FXT string keys and GXT key hashes. Associate each localized file with its owning mod and base resource so identical keys from different mods retain their existing priority.
- [ ] Resolve translations per key. When the game requests an FXT key, resolve its base resource using the existing text lookup order. Look for the same key in that resource's translation for the active language. If the translated file or key is missing, return the English value from the base GXT or FXT. If the base resource also lacks the key, continue through the existing lookup chain.
- [ ] Update the text overlays when the player switches languages, including when they return to a stock language. Check compatibility with Mod Loader and CLEO FXT lookups.

### Example: a translated mod menu

The installed Mod Loader uses `.data/text/1033/menu.fxt` for its English menu.
For a mod with an English `menu.gxt`, the proposed equivalent would be:

```text
menu.gxt          English base text
menu.russian.gxt  Russian text for the same keys
```

An author would add a translation for each existing menu key they want to
localize. For example, if the English file defines `MOD_OPT` and the Russian
file has that same key, selecting Russian (language code `5`) would use the
Russian entry. A key omitted from `menu.russian.gxt` would still display its
English entry from `menu.gxt`. The same rule would apply to an English FXT
base, with the translated GXT linked to it explicitly.
