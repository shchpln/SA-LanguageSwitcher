# Language ID contract

On GTA San Andreas 1.0 US Hoodlum (x86), integrations can read one byte from
`CMenuManager::m_nPrefsLanguage` at `0xBA67CC` while the game is running.
Codes describe the active language. The INI section ID identifies the
specific translation pack. GTA saves the language code in `gta_sa.set`, and
LanguageSwitcher saves the pack ID and the last stock code in its INI.

| Code | Language | Locale | INI name prefix |
| ---: | --- | --- | --- |
| 0 | English (US) | `en-US` | Stock |
| 1 | French | `fr` | Stock |
| 2 | German | `de` | Stock |
| 3 | Italian | `it` | Stock |
| 4 | Spanish | `es` | Stock |
| 5 | Russian | `ru` | `Russian` |
| 6 | Japanese | `ja` | `Japanese` |
| 7 | Polish | `pl` | `Polish` |
| 8 | Brazilian Portuguese | `pt-BR` | `BrazilianPortuguese` |
| 9 | Portuguese | `pt-PT` | `Portuguese` |
| 10 | Simplified Chinese | `zh-Hans` | `ChineseSimplified` |
| 11 | Traditional Chinese | `zh-Hant` | `ChineseTraditional` |
| 12 | Korean | `ko` | `Korean` |
| 13 | Turkish | `tr` | `Turkish` |
| 14 | Ukrainian | `uk` | `Ukrainian` |
| 15–63 | Reserved for future public assignments | | |
| 64–127 | Private assignments | | |

Public assignments are stable. An integration should handle unknown codes and
should avoid treating private codes as universal language identifiers.
Values 128–255 are outside this contract because the original executable
reads the field as a signed byte in some places.

The INI name prefix is case-insensitive. A registered pack gets the matching
code automatically. It can use an optional suffix to distinguish variants,
for example `[Language.Russian.SanLTD]` and `[Language.Russian.MSLK]` both
publish 5. An explicit `LanguageId` for a registered pack must match the table.
Unregistered packs can set an unused private code. A malformed, reserved, or
duplicate private value leaves that pack on the stock code. An invalid
override of a registered code keeps its registered value.

On startup, the ASI temporarily substitutes the saved stock code when
GTA reads an extended code. Once the menu is ready, the ASI restores the
pack selected in the INI or uses the first available pack matching the
settings file code. If no pack matches, the stock language remains active.

Switch back to a stock language before removing the ASI. Unpatched Hoodlum
expects a code from 0 to 4 in `gta_sa.set`. If a compatibility hook cannot
be installed, the menu can still switch packs while retaining the stock code
in memory and in the settings file.

Registered codes identify languages independently of the glyph encodings
supported by their individual GXT and font files.
