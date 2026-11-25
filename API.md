# Language Switcher API

`LanguageSwitcher.asi` exports two C functions with the x86 `__cdecl`
calling convention. Resolve them by name after the ASI loader has loaded
the module. Call them on the game's main thread.

| Export | Result |
| --- | --- |
| `int GetCurrentLanguageId(void)` | The active code from [LANGUAGE_IDS.md](LANGUAGE_IDS.md), or `-1` if the game build is unsupported or the active package has no assigned code. |
| `const char* GetCurrentLanguageName(void)` | A lowercase, NUL-terminated UTF-8 language name, or `nullptr` if the name is unavailable. The ASI owns the returned string. Copy it if you need it after the module unloads. |

Stock and registered languages use fixed lowercase English names. For
example, every registered Russian pack returns `5` and `russian`. A private
or unregistered pack uses its configured `MenuLabel` converted to lowercase
with invariant Unicode casing. Game menu labels keep their configured case.
The API reports the active pack even if a
compatibility conflict prevents publishing its code to GTA's preference byte.
While a saved pack is waiting for menu initialization, the exports report
the stock language the game is currently using.

```cpp
#include <Windows.h>

using GetLanguageId = int (__cdecl*)(void);
using GetLanguageName = const char* (__cdecl*)(void);

void UpdateLanguage() {
    const auto module = GetModuleHandleW(L"LanguageSwitcher.asi");
    if (!module) return;

    const auto getId = reinterpret_cast<GetLanguageId>(
        GetProcAddress(module, "GetCurrentLanguageId"));
    const auto getName = reinterpret_cast<GetLanguageName>(
        GetProcAddress(module, "GetCurrentLanguageName"));
    if (!getId || !getName) return;

    const int languageId = getId();
    const char* languageName = getName();
    // Use the current language in your mod.
}
```
