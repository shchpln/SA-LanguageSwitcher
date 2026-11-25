#include <Windows.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "CFont.h"
#include "CMenuManager.h"
#include "CText.h"
#include "CTxdStore.h"
#include "GameVersion.h"
#include "GxtValidation.h"

namespace {
namespace fs = std::filesystem;
using FontPair = std::array<tFontData, 2>;
using Textures = std::array<RwTexture*, 2>;

struct Language {
    std::string id, label, nativeLabel, apiName, gxt, key, txd;
    char languageCode = 0;
    FontPair metrics{};
    Textures textures{};
};

fs::path directory, gameRoot, ini;
std::ofstream logFile;
std::vector<Language> languages;
Language* active = nullptr;
char stockLanguage = 0;
char savedStockLanguage = 0;
int savedGameLanguage = -1;
uintptr_t mainGxtOpen = 0;
uintptr_t missionGxtOpen = 0;
FontPair originalMetrics{};
Textures originalTextures{};
int firstCustom = 0;
bool initialized = false;
bool publishLanguageCodes = false;
bool supportedGame = false;
constexpr auto languagePage = MENUPAGE_LANGUAGE_SETTINGS;

struct LanguageIdentity {
    std::wstring_view sectionPrefix;
    const char* name;
};

// Array indices are the stable public codes in LANGUAGE_IDS.md.
constexpr LanguageIdentity languageIdentities[] = {
    {L"", "english"},
    {L"", "french"},
    {L"", "german"},
    {L"", "italian"},
    {L"", "spanish"},
    {L"Russian", "russian"},
    {L"Japanese", "japanese"},
    {L"Polish", "polish"},
    {L"BrazilianPortuguese", "brazilian portuguese"},
    {L"Portuguese", "portuguese"},
    {L"ChineseSimplified", "simplified chinese"},
    {L"ChineseTraditional", "traditional chinese"},
    {L"Korean", "korean"},
    {L"Turkish", "turkish"},
    {L"Ukrainian", "ukrainian"}
};

std::wstring Read(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    wchar_t value[1024]{};
    GetPrivateProfileStringW(section, key, fallback, value, 1024, ini.c_str());
    return value;
}

std::string EncodeApiName(std::wstring_view name) {
    // Use invariant Unicode casing so the API is independent of Windows locale.
    const auto length = static_cast<int>(name.size());
    const int count = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        name.data(), length, nullptr, 0, nullptr, nullptr, 0);
    if (count <= 0) return {};
    std::wstring lower(count, L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        name.data(), length, lower.data(), count, nullptr, nullptr, 0)) return {};
    const auto utf8 = fs::path(lower).u8string();
    return std::string(utf8.begin(), utf8.end());
}

char RegisteredLanguageCode(std::wstring_view packageId) {
    // A suffix distinguishes translations of the same registered language.
    const auto language = packageId.substr(0, packageId.find(L'.'));
    for (size_t code = 5; code < std::size(languageIdentities); ++code) {
        const auto name = languageIdentities[code].sectionPrefix;
        if (language.size() == name.size() && _wcsnicmp(language.data(), name.data(), name.size()) == 0)
            return static_cast<char>(code);
    }
    return 0;
}

fs::path ResolveResource(const fs::path& configuredPath) {
    if (configuredPath.empty() || configuredPath.is_absolute()) return configuredPath;
    const auto local = (directory / configuredPath).lexically_normal();
    std::error_code error;
    if (fs::is_regular_file(local, error)) return local;
    return (gameRoot / configuredPath).lexically_normal();
}

std::string EncodeMenuLabel(std::wstring_view label, bool russianAtlas) {
    // Russian Localization Support uses Latin lookalikes and custom GXT bytes.
    // The contiguous Cyrillic glyphs are documented in the package's fonts.dat.
    constexpr std::wstring_view cyrillic = L"БФГДЖИЙЛЗЦЩВПЧШТЪЫЬЭЮЯМбфгджийлзцщвпчштъыьэюяНнм";
    constexpr std::wstring_view lookalikes = L"АЕКОРСУХаеокрсух";
    constexpr std::string_view latin = "AEKOPCYXaeokpcyx";
    std::string result;
    for (const auto character : label) {
        if (character >= 32 && character < 127) {
            result.push_back(static_cast<char>(character));
            continue;
        }
        if (!russianAtlas) return {};
        const auto latinIndex = lookalikes.find(character);
        if (latinIndex != std::wstring_view::npos) {
            result.push_back(latin[latinIndex]);
            continue;
        }
        const auto glyph = cyrillic.find(character);
        // The first atlas leaves these three uppercase glyphs empty.
        if (glyph == std::wstring_view::npos || (glyph >= 16 && glyph <= 18)) return {};
        result.push_back(static_cast<char>(0x80 + glyph));
    }
    return result;
}

bool ReadMetrics(const fs::path& path, int id, tFontData& result) {
    // Read this language's table without changing the file used by the stock loader.
    std::ifstream file(path);
    std::string line, tokens;
    while (std::getline(file, line)) {
        const auto comment = line.find('#');
        tokens += line.substr(0, comment) + ' ';
    }
    std::istringstream input(tokens);
    std::string token;
    while (input >> token) {
        if (token != "[FONT_ID]") continue;
        int sectionId = -1;
        if (!(input >> sectionId) || sectionId != id) continue;
        const auto readByte = [&](char& byte) {
            int number = -1;
            if (!(input >> number) || number < 0 || number > 255) return false;
            byte = static_cast<char>(number);
            return true;
        };
        if (!(input >> token) || token != "[PROP]") return false;
        for (auto& width : result.m_propValues)
            if (!readByte(width)) return false;
        return (input >> token) && token == "[UNPROP]" && readByte(result.m_unpropValue)
            && (input >> token) && token == "[REPLACEMENT_SPACE_CHAR]" && readByte(result.m_spaceValue);
    }
    return false;
}

void ReadLanguages() {
    // The ASI loader can call DllMain on a thread with a small stack.
    std::vector<wchar_t> sections(32768);
    GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), ini.c_str());
    std::vector<std::wstring> names;
    for (const auto* ptr = sections.data(); *ptr; ptr += wcslen(ptr) + 1) {
        if (std::wstring_view(ptr).starts_with(L"Language.") && wcslen(ptr) > 9)
            names.emplace_back(ptr);
    }
    if (names.empty()) names.emplace_back(L"Language.Russian");
    for (const auto& section : names) {
        const bool russian = section == L"Language.Russian";
        Language lang;
        lang.id = fs::path(section.substr(9)).string();
        const auto menuLabel = Read(section.c_str(), L"MenuLabel", section.c_str() + 9);
        lang.label = EncodeMenuLabel(menuLabel, false);
        if (lang.label.empty()) lang.label = lang.id;
        lang.apiName = EncodeApiName(menuLabel.empty() ? section.substr(9) : menuLabel);
        lang.nativeLabel = EncodeMenuLabel(
            Read(section.c_str(), L"NativeMenuLabel", russian ? L"Русский" : L""), russian);
        const auto registered = RegisteredLanguageCode(std::wstring_view(section).substr(9));
        const auto code = Read(section.c_str(), L"LanguageId", L"");
        lang.languageCode = registered;
        if (!code.empty()) {
            wchar_t* end = nullptr;
            const auto number = wcstol(code.c_str(), &end, 10);
            const bool privateCode = number >= 64 && number <= 127
                && std::ranges::none_of(languages, [number](const Language& other) {
                    return other.languageCode == number;
                });
            if (*end == L'\0' && (registered ? number == registered : privateCode)) {
                lang.languageCode = static_cast<char>(number);
            } else if (logFile) {
                logFile << "LanguageId does not match the registry or available private codes for "
                        << lang.id << ": "
                        << fs::path(code) << std::endl;
            }
        }

        const auto metricsPath = ResolveResource(
            Read(section.c_str(), L"FontMetricsFile", russian ? L"data\\fonts_ru.dat" : L""));
        const auto txdPath = ResolveResource(
            Read(section.c_str(), L"FontTextureFile", russian ? L"models\\fonts_ru.txd" : L""));
        std::error_code fileError;
        if (!fs::is_regular_file(txdPath, fileError)) {
            if (logFile) logFile << "Missing TXD for " << lang.id << ": " << txdPath << std::endl;
            continue;
        }
        lang.txd = txdPath.string();
        const auto path = Read(section.c_str(), L"TextFile", russian ? L"text\\russian.gxt" : L"");
        const auto gxtPath = ResolveResource(path);
        if (!gxt::IsValidPcFile(gxtPath)
            || !ReadMetrics(metricsPath, 0, lang.metrics[0]) || !ReadMetrics(metricsPath, 1, lang.metrics[1])) {
            if (logFile) logFile << "Invalid GXT or font metrics for " << lang.id
                                 << ": " << gxtPath << ", " << metricsPath << std::endl;
            continue;
        }
        lang.gxt = gxtPath.string();
        languages.push_back(std::move(lang));
    }
}

void SwitchTo(Language* language, bool saveSettings = false) {
    if (active == language) return;
    // On first activation save the live atlas and metrics, including Mod Loader overrides.
    if (!active) {
        const auto preference = static_cast<unsigned char>(FrontEndMenuManager.m_nPrefsLanguage);
        stockLanguage = preference <= 4 ? static_cast<char>(preference) : 0;
        for (size_t i = 0; i < 2; ++i) {
            originalMetrics[i] = gFontData[i];
            originalTextures[i] = CFont::Sprite[i].m_pTexture;
        }
    }
    CFont::DrawFonts();
    const auto* previous = active;
    active = language;
    if (publishLanguageCodes)
        FrontEndMenuManager.m_nPrefsLanguage = language && language->languageCode
            ? language->languageCode : stockLanguage;
    for (size_t i = 0; i < 2; ++i) {
        // Each live sprite owns one reference, independently of the retained TXD.
        if (language) (void)RwTextureAddRef(language->textures[i]);
        gFontData[i] = language ? language->metrics[i] : originalMetrics[i];
        CFont::Sprite[i].m_pTexture = language ? language->textures[i] : originalTextures[i];
        if (previous) RwTextureDestroy(previous->textures[i]);
    }
    // The stock loader continues to use the native preferences and mission-table machinery.
    auto load = reinterpret_cast<void(__thiscall*)(CText*, bool)>(0x6A01A0);
    char missionName[16]{};
    TheText.getMissionTableName(missionName);
    load(&TheText, false);
    if (missionName[0]) TheText.LoadMissionTable(missionName);
    const auto selection = language ? fs::path(language->id).wstring() : std::wstring{};
    WritePrivateProfileStringW(L"Selection", L"Language", selection.c_str(), ini.c_str());
    if (language && publishLanguageCodes) {
        // Save a stock fallback before allowing the game's own settings writer
        // to persist an extended language code in gta_sa.set.
        const auto fallback = std::to_wstring(stockLanguage);
        if (WritePrivateProfileStringW(L"Selection", L"StockLanguage", fallback.c_str(), ini.c_str())) {
            if (saveSettings || savedGameLanguage != FrontEndMenuManager.m_nPrefsLanguage)
                FrontEndMenuManager.SaveSettings();
        } else if (logFile) {
            logFile << "Could not save stock language fallback." << std::endl;
        }
    }
}

// These Hoodlum readers originally load m_nPrefsLanguage into EAX. GTA's
// GXT name tables and font scaling retain the stock language while a pack
// exposes its custom code through the public preference byte.
__declspec(naked) void ReadStockLanguage() {
    __asm {
        cmp dword ptr [active], 0
        je readPreference
        movsx eax, byte ptr [stockLanguage]
        ret
    readPreference:
        movsx eax, byte ptr ds:[0BA67CCh]
        ret
    }
}

// Hoodlum's SCM opcode 09FB loads the preference into ECX at 0x47B4EC.
// Stock shop scripts branch on codes 0–4 before creating their menu panels.
// Give them the stock code while keeping the extended code visible to mods.
__declspec(naked) void ReadScriptStockLanguage() {
    __asm {
        pushfd
        cmp dword ptr [active], 0
        je readPreference
        movsx ecx, byte ptr [stockLanguage]
        popfd
        ret
    readPreference:
        movsx ecx, byte ptr ds:[0BA67CCh]
        popfd
        ret
    }
}

// LoadSettings can request a stock GXT before the renderer and language TXDs
// are ready. Stash the saved code and supply a valid stock preference meanwhile.
int __cdecl ReadSavedLanguage(void* file, char* value, int size) {
    auto read = reinterpret_cast<int(__cdecl*)(void*, char*, int)>(0x538950);
    const auto count = read(file, value, size);
    if (count == 1 && size == 1) {
        savedGameLanguage = static_cast<unsigned char>(*value);
        if (savedGameLanguage > 4) {
            *value = savedStockLanguage;
            if (logFile) logFile << "Settings language code " << savedGameLanguage
                                 << "; using stock code " << static_cast<int>(savedStockLanguage)
                                 << " until the menu initializes." << std::endl;
        }
    }
    return count;
}

// Both main and mission GXT opens call CFileMgr::OpenFile in the verified Hoodlum executable.
void* __cdecl OpenGxt(const char* path, const char* mode) {
    auto open = reinterpret_cast<void*(__cdecl*)(const char*, const char*)>(0x538900);
    if (active) {
        if (auto* file = open(active->gxt.c_str(), mode)) return file;
    }
    return open(path, mode);
}

// Preserve the game return address when forwarding stock opens. Mod Loader
// can use that return address to identify the resource being opened.
__declspec(naked) void* __cdecl OpenMainGxt(const char*, const char*) {
    __asm {
        cmp dword ptr [active], 0
        jne useCustom
        jmp dword ptr [mainGxtOpen]
    useCustom:
        jmp OpenGxt
    }
}

__declspec(naked) void* __cdecl OpenMissionGxt(const char*, const char*) {
    __asm {
        cmp dword ptr [active], 0
        jne useCustom
        jmp dword ptr [missionGxtOpen]
    useCustom:
        jmp OpenGxt
    }
}

const char* __fastcall GetText(CText* text, void*, const char* key) {
    if (key && key[0] == 'L' && key[1] == 'S') {
        for (const auto& lang : languages)
            if (lang.key == key) {
                return active == &lang && !lang.nativeLabel.empty() ? lang.nativeLabel.c_str() : lang.label.c_str();
            }
    }
    using Getter = const char*(__thiscall*)(CText*, const char*);
    return reinterpret_cast<Getter>(0x6A0050)(text, key);
}

void __fastcall ProcessOptions(CMenuManager* menu, void*, char input, char* exit, char enter) {
    bool returningToStock = false;
    if (initialized && firstCustom && menu->m_nCurrentMenuPage == languagePage && enter) {
        const auto index = menu->m_nCurrentMenuEntry;
        if (index >= firstCustom && static_cast<size_t>(index - firstCustom) < languages.size()) {
            SwitchTo(&languages[index - firstCustom], true);
            return;
        }
        if (index >= 0 && index < firstCustom && active) {
            returningToStock = publishLanguageCodes;
            SwitchTo(nullptr);
        }
    }
    auto process = reinterpret_cast<void(__thiscall*)(CMenuManager*, char, char*, char)>(0x576FE0);
    process(menu, input, exit, enter);
    // GTA may skip its settings write when the restored stock code already
    // equals the selected language. Replace the previous custom byte anyway.
    if (returningToStock) menu->SaveSettings();
}

CMenuScreen* FindLanguageScreen() {
    // Mod Loader 0.3.10 relocates the menu table. Read the live x86 operands
    // used for row text, the selected action, and the selected entry type.
    // These instruction forms were verified in GTA SA 1.0 US Hoodlum.
    constexpr uintptr_t sites[] = {0x579B88, 0x577014, 0x57B699};
    constexpr unsigned char opcodes[][3] = {
        {0x8D, 0xBC, 0x01}, {0x0F, 0xB6, 0xB9}, {0x8A, 0x8C, 0x51}
    };
    constexpr uintptr_t offsets[] = {0xB, 0xA, 0x13};
    uintptr_t table = 0;
    for (size_t i = 0; i < std::size(sites); ++i) {
        const auto* instruction = reinterpret_cast<const unsigned char*>(sites[i]);
        if (memcmp(instruction, opcodes[i], sizeof opcodes[i])) return nullptr;
        uintptr_t operand;
        memcpy(&operand, instruction + sizeof opcodes[i], sizeof operand);
        if (operand < offsets[i]) return nullptr;
        const auto candidate = operand - offsets[i];
        if (i && candidate != table) return nullptr;
        table = candidate;
    }
    const auto address = table + languagePage * sizeof(CMenuScreen);
    auto* screen = reinterpret_cast<CMenuScreen*>(address);
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(screen, &memory, sizeof memory) || memory.State != MEM_COMMIT
        || (memory.Protect & PAGE_GUARD)
        || !(memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
        || address - reinterpret_cast<uintptr_t>(memory.BaseAddress) + sizeof(*screen) > memory.RegionSize)
        return nullptr;
    return screen;
}

void InitializeMenu() {
    if (initialized || !CFont::Sprite[0].m_pTexture || !CFont::Sprite[1].m_pTexture) return;
    initialized = true;
    auto* screen = FindLanguageScreen();
    if (!screen) {
        if (logFile) logFile << "Could not validate the active Language menu table." << std::endl;
        return;
    }
    auto& entries = screen->m_aEntries;
    int back = 0;
    while (back < static_cast<int>(std::size(entries)) && entries[back].m_nAction >= MENUACTION_LANG_ENG
            && entries[back].m_nAction <= MENUACTION_LANG_SPA) ++back;
    if (back == 0 || back + 2 >= static_cast<int>(std::size(entries)) || entries[back].m_nAction != 2
        || entries[back + 1].m_nAction != 0) {
        if (logFile) logFile << "Incompatible Language menu layout at "
                             << static_cast<const void*>(screen) << std::endl;
        return;
    }

    const size_t capacity = std::min<size_t>(6, std::size(entries) - back - 2);
    size_t loaded = 0;
    unsigned int packageIndex = 0;
    std::erase_if(languages, [&](Language& lang) {
        if (loaded == capacity) return true;
        const auto slotName = "langswitch_" + std::to_string(packageIndex++);
        const int txd = CTxdStore::AddTxdSlot(slotName.c_str());
        if (txd < 0) {
            if (logFile) logFile << "Could not allocate TXD slot for " << lang.id << std::endl;
            return true;
        }
        if (!CTxdStore::LoadTxd(txd, lang.txd.c_str())) {
            CTxdStore::RemoveTxdSlot(txd);
            if (logFile) logFile << "Could not load TXD for " << lang.id << ": " << lang.txd << std::endl;
            return true;
        }
        auto* dictionary = CTxdStore::ms_pTxdPool->GetAt(txd);
        if (dictionary && dictionary->m_pRwDictionary) {
            // Hoodlum CFont::Initialise assigns font2 to slot 0 and font1 to slot 1.
            lang.textures[0] = RwTexDictionaryFindNamedTexture(dictionary->m_pRwDictionary, "font2");
            lang.textures[1] = RwTexDictionaryFindNamedTexture(dictionary->m_pRwDictionary, "font1");
        }
        if (!lang.textures[0] || !lang.textures[1]) {
            CTxdStore::RemoveTxdSlot(txd);
            if (logFile) logFile << "Missing font1 or font2 for " << lang.id << std::endl;
            return true;
        }
        // Keep the dictionary alive while the language can be selected.
        CTxdStore::AddRef(txd);
        ++loaded;
        return false;
    });
    if (languages.empty()) return;
    firstCustom = back;
    const auto backEntry = entries[back];
    const auto row = entries[0];
    for (size_t i = 0; i < languages.size(); ++i) {
        auto& lang = languages[i];
        lang.key = "LS" + std::to_string(i);
        auto& entry = entries[back + i];
        entry = row;
        // As with the stock Spanish row, zero coordinates make Hoodlum
        // position this row using the preceding entry and the native spacing.
        entry.m_nX = 0;
        entry.m_nY = 0;
        memset(entry.m_EntryName, 0, sizeof entry.m_EntryName);
        strcpy_s(entry.m_EntryName, lang.key.c_str());
    }
    entries[back + languages.size()] = backEntry;
    entries[back + languages.size() + 1] = {};

    const auto saved = Read(L"Selection", L"Language", L"");
    for (auto& lang : languages) {
        if (fs::path(lang.id).wstring() == saved) {
            SwitchTo(&lang);
            return;
        }
    }
    // If the INI lost its pack ID, restore the first installed pack with
    // the saved language code. Variants share a code but have distinct IDs.
    if (publishLanguageCodes && savedGameLanguage >= 5 && savedGameLanguage <= 127) {
        for (auto& lang : languages) {
            if (lang.languageCode == savedGameLanguage) {
                SwitchTo(&lang);
                return;
            }
        }
    }
}

void __fastcall DrawMenu(CMenuManager* menu, void*) {
    InitializeMenu();
    auto original = reinterpret_cast<void(__thiscall*)(CMenuManager*)>(0x57C290);
    original(menu);
}

struct Patch {
    uintptr_t site;
    unsigned char opcode;
    uintptr_t target;
    uintptr_t replacement;
    uintptr_t* forwardedTarget = nullptr;
    unsigned char languageReadOperand = 0x05; // ModR/M byte for movsx EAX.

    size_t Length() const { return target ? 5 : 7; }
};

bool ApplyHooks(std::span<const Patch> patches) {
    for (const auto& patch : patches) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(patch.site);
        if (patch.target) {
            uint32_t displacement;
            memcpy(&displacement, bytes + 1, sizeof displacement);
            const auto actualTarget = patch.site + 5 + displacement;
            if (logFile) logFile << "Hook 0x" << std::hex << patch.site << ": opcode 0x"
                                 << static_cast<unsigned int>(bytes[0]) << ", target 0x"
                                 << actualTarget << ", stock target 0x" << patch.target
                                 << std::dec << std::endl;
            if (patch.forwardedTarget && bytes[0] == patch.opcode) {
                // Mod Loader 0.3.10 redirects these same CALL instructions.
                MEMORY_BASIC_INFORMATION memory{};
                if (VirtualQuery(reinterpret_cast<const void*>(actualTarget), &memory, sizeof memory)
                    && memory.State == MEM_COMMIT && !(memory.Protect & PAGE_GUARD)
                    && (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
                    *patch.forwardedTarget = actualTarget;
                    continue;
                }
            }
            if (bytes[0] != patch.opcode || actualTarget != patch.target) return false;
        } else {
            const unsigned char stockRead[] = {0x0F, 0xBE, patch.languageReadOperand, 0xCC, 0x67, 0xBA, 0x00};
            if (memcmp(bytes, stockRead, sizeof stockRead) != 0) {
                if (logFile) logFile << "Unexpected language read at 0x" << std::hex
                                     << patch.site << std::dec << std::endl;
                return false;
            }
        }
    }
    // Change protection on every range before writing any hook.
    std::vector<DWORD> previous(patches.size());
    size_t writable = 0;
    for (const auto& patch : patches) {
        if (!VirtualProtect(reinterpret_cast<void*>(patch.site), patch.Length(), PAGE_EXECUTE_READWRITE, &previous[writable]))
            break;
        ++writable;
    }
    if (writable == std::size(patches)) {
        for (const auto& patch : patches) {
            auto* bytes = reinterpret_cast<unsigned char*>(patch.site);
            const auto displacement = patch.replacement - patch.site - 5;
            bytes[0] = patch.opcode;
            memcpy(bytes + 1, &displacement, sizeof displacement);
            if (patch.Length() > 5) memset(bytes + 5, 0x90, patch.Length() - 5);
            FlushInstructionCache(GetCurrentProcess(), bytes, patch.Length());
        }
    }
    for (size_t i = writable; i > 0; --i) {
        DWORD ignored;
        VirtualProtect(reinterpret_cast<void*>(patches[i - 1].site), patches[i - 1].Length(), previous[i - 1], &ignored);
    }
    return writable == std::size(patches);
}

bool InstallHooks(bool settingsReaderAvailable) {
    // Addresses and call targets verified with dumpbin against the workspace Hoodlum executable.
    const Patch menuPatches[] = {
        {0x6A0228, 0xE8, 0x538900, reinterpret_cast<uintptr_t>(&OpenMainGxt), &mainGxtOpen},
        {0x69FD5A, 0xE8, 0x538900, reinterpret_cast<uintptr_t>(&OpenMissionGxt), &missionGxtOpen},
        {0x57B66F, 0xE8, 0x576FE0, reinterpret_cast<uintptr_t>(&ProcessOptions)},
        {0x57B702, 0xE8, 0x576FE0, reinterpret_cast<uintptr_t>(&ProcessOptions)},
        {0x53E82D, 0xE8, 0x57C290, reinterpret_cast<uintptr_t>(&DrawMenu)},
        {0x53EB8C, 0xE8, 0x57C290, reinterpret_cast<uintptr_t>(&DrawMenu)},
        // Only resolve custom keys at the native menu's row text call sites.
        {0x579D73, 0xE8, 0x6A0050, reinterpret_cast<uintptr_t>(&GetText)},
        {0x57FE2F, 0xE8, 0x6A0050, reinterpret_cast<uintptr_t>(&GetText)}
    };
    if (!ApplyHooks(menuPatches)) return false;
    if (!settingsReaderAvailable
        || std::ranges::none_of(languages, [](const Language& lang) { return lang.languageCode != 0; }))
        return true;
    const Patch languagePatches[] = {
        {0x6A01C8, 0xE8, 0, reinterpret_cast<uintptr_t>(&ReadStockLanguage)},
        {0x69FCEB, 0xE8, 0, reinterpret_cast<uintptr_t>(&ReadStockLanguage)},
        {0x7193A0, 0xE8, 0, reinterpret_cast<uintptr_t>(&ReadStockLanguage)},
        {0x47B4EC, 0xE8, 0, reinterpret_cast<uintptr_t>(&ReadScriptStockLanguage), nullptr, 0x0D}
    };
    // Retain menu support when a third-party patch owns one of these sites.
    publishLanguageCodes = ApplyHooks(languagePatches);
    if (!publishLanguageCodes && logFile)
        logFile << "Language codes remain at their stock values." << std::endl;
    return true;
}
}

// Exported names and x86 calling conventions are declared in LanguageSwitcher.def.
// Callers use these from the game thread, where the active pack is changed.
extern "C" int __cdecl GetCurrentLanguageId() {
    if (!supportedGame) return -1;
    if (active) return active->languageCode ? active->languageCode : -1;
    const int code = static_cast<unsigned char>(FrontEndMenuManager.m_nPrefsLanguage);
    return code <= 127 ? code : -1;
}

extern "C" const char* __cdecl GetCurrentLanguageName() {
    const auto code = GetCurrentLanguageId();
    if (code >= 0 && static_cast<size_t>(code) < std::size(languageIdentities))
        return languageIdentities[code].name;
    return active && !active->apiName.empty() ? active->apiName.c_str() : nullptr;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    try {
        std::vector<wchar_t> path(32768);
        const auto capacity = static_cast<DWORD>(path.size());
        const auto pluginPathLength = GetModuleFileNameW(self, path.data(), capacity);
        if (!pluginPathLength || pluginPathLength == capacity) return TRUE;
        directory = fs::path(path.data()).parent_path();
        const auto executablePathLength = GetModuleFileNameW(nullptr, path.data(), capacity);
        if (!executablePathLength || executablePathLength == capacity) return TRUE;
        gameRoot = fs::path(path.data()).parent_path();
        ini = directory / L"LanguageSwitcher.ini";
        if (Read(L"General", L"EnableLogging", L"1") != L"0")
            logFile.open(directory / L"LanguageSwitcher.log", std::ios::trunc);
        const auto version = plugin::GetGameVersion();
        if (logFile) logFile << "Game: " << fs::path(path.data())
                             << ", plugin-sdk version ID: " << version << std::endl;
        if (version != GAME_10US_HOODLUM) {
            if (logFile) logFile << "Unsupported game build. No hooks installed." << std::endl;
            return TRUE;
        }
        supportedGame = true;
        const auto fallback = GetPrivateProfileIntW(L"Selection", L"StockLanguage", 0, ini.c_str());
        savedStockLanguage = fallback <= 4 ? static_cast<char>(fallback) : 0;
        // Install the reader even if the switcher is disabled or its pack was
        // removed, so a previously saved custom code remains safe to read.
        const Patch settingsRead[] = {
            {0x57CABB, 0xE8, 0x538950, reinterpret_cast<uintptr_t>(&ReadSavedLanguage)}
        };
        const bool settingsReaderAvailable = ApplyHooks(settingsRead);
        if (!settingsReaderAvailable && logFile)
            logFile << "Cannot preserve extended language codes in gta_sa.set." << std::endl;
        if (Read(L"General", L"Enabled", L"1") == L"0") return TRUE;
        ReadLanguages();
        if (!languages.empty() && !InstallHooks(settingsReaderAvailable) && logFile)
            logFile << "Menu hook installation failed." << std::endl;
    } catch (...) {
        if (logFile) logFile << "Initialization failed." << std::endl;
    }
    return TRUE;
}
