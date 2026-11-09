#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include <array>
#include <cstdint>
#include <cwchar>

namespace {
using VolumeFn = void (__thiscall*)(void*, uint32_t, uint32_t, int);
using SessionFn = void (__thiscall*)(void*);
using PlaybackFn = void (__thiscall*)(void*, uintptr_t, uintptr_t, uintptr_t);
VolumeFn setVolume;
SessionFn closeSession;
PlaybackFn playback;
volatile LONG status;
volatile LONG ownerThread;
volatile LONG restoredCount;
struct Volume { uint32_t low, high; int value; };
std::array<Volume, 64> saved{};
size_t savedCount;
bool restorePending = true;
uint32_t loadedChannel;
wchar_t settingsPath[MAX_PATH];

struct Settings {
    uint32_t magic;
    uint32_t count;
    std::array<Volume, 64> entries;
};
constexpr uint32_t settingsMagic = 0x31565959; // YYV1, little endian.
static_assert(sizeof(Volume) == 12 && sizeof(Settings) == 776);

void LoadSettings(uint32_t channel) {
    if (loadedChannel == channel) return;
    loadedChannel = channel;
    saved = {};
    savedCount = 0;
    const DWORD imageLength = GetModuleFileNameW(nullptr, settingsPath, ARRAYSIZE(settingsPath));
    const auto directory = imageLength && imageLength < ARRAYSIZE(settingsPath)
        ? std::wcsrchr(settingsPath, L'\\') : nullptr;
    const size_t length = directory ? static_cast<size_t>(directory - settingsPath) : 0;
    if (!length || swprintf_s(settingsPath + length, ARRAYSIZE(settingsPath) - length,
                              L"\\YYVolume%u.bin", channel) < 0) {
        settingsPath[0] = 0;
        return;
    }
    HANDLE file = CreateFileW(settingsPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    Settings data{};
    LARGE_INTEGER size{};
    DWORD read = 0;
    const bool valid = GetFileSizeEx(file, &size) && size.QuadPart == sizeof(data) &&
        ReadFile(file, &data, sizeof(data), &read, nullptr) && read == sizeof(data) &&
        data.magic == settingsMagic && data.count <= saved.size();
    CloseHandle(file);
    if (valid) {
        saved = data.entries;
        savedCount = data.count;
    }
}

void SaveSettings() {
    if (!settingsPath[0]) return;
    wchar_t temp[MAX_PATH];
    if (swprintf_s(temp, L"%ls.%lu.tmp", settingsPath, GetCurrentProcessId()) < 0) return;
    HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const Settings data{settingsMagic, static_cast<uint32_t>(savedCount), saved};
    DWORD written = 0;
    const bool complete = WriteFile(file, &data, sizeof(data), &written, nullptr) &&
        written == sizeof(data) && FlushFileBuffers(file);
    CloseHandle(file);
    if (!complete || !MoveFileExW(temp, settingsPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        DeleteFileW(temp);
}
bool OnVoiceThread() {
    const auto current = static_cast<LONG>(GetCurrentThreadId());
    const auto previous = InterlockedCompareExchange(&ownerThread, current, 0);
    return !previous || previous == current;
}

BOOL CALLBACK CheckChannel(HWND window, LPARAM result) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    wchar_t title[256];
    if (GetWindowTextW(window, title, ARRAYSIZE(title)) < 2) return TRUE;
    const wchar_t* suffix = title;
    uint32_t channel = 0;
    while (*suffix >= L'0' && *suffix <= L'9') {
        const auto digit = static_cast<uint32_t>(*suffix++ - L'0');
        if (channel > (UINT32_MAX - digit) / 10) return TRUE;
        channel = channel * 10 + digit;
    }
    while (*suffix == L' ') ++suffix;
    if (!channel || *suffix != L'-') return TRUE;
    *reinterpret_cast<uint32_t*>(result) = channel;
    return FALSE;
}

uint32_t CurrentChannel() {
    uint32_t result = 0;
    EnumWindows(CheckChannel, reinterpret_cast<LPARAM>(&result));
    return result;
}
bool AudioValid(void* voice) {
    return static_cast<const uint8_t*>(voice)[0x7a4] == 1;
}

void __fastcall SetVolume(void* object, void*, uint32_t low, uint32_t high, int value) {
    setVolume(object, low, high, value);
    const auto channel = CurrentChannel();
    if (!(low || high) || !channel || !OnVoiceThread()) return;
    LoadSettings(channel);
    for (size_t i = 0; i < savedCount; ++i) {
        if (saved[i].low == low && saved[i].high == high) {
            if (saved[i].value != value) {
                saved[i].value = value;
                SaveSettings();
            }
            return;
        }
    }
    if (savedCount < saved.size()) {
        saved[savedCount++] = {low, high, value};
        SaveSettings();
    }
}

void __fastcall CloseSession(void* voice, void*) {
    if (OnVoiceThread()) {
        restorePending = true;
        loadedChannel = 0;
    }
    closeSession(voice);
}

void __fastcall Playback(void* voice, void*, uintptr_t a, uintptr_t b, uintptr_t c) {
    playback(voice, a, b, c);
    if (!AudioValid(voice) || !OnVoiceThread() || !restorePending) return;
    const auto channel = CurrentChannel();
    if (!channel) return;
    restorePending = false;
    auto head = *reinterpret_cast<void***>(static_cast<uint8_t*>(voice) + 0x79c);
    if (!head || *head != head) return;
    LoadSettings(channel);
    if (!savedCount) return;
    // The original setter is on the voice object's secondary interface.
    for (size_t i = 0; i < savedCount; ++i) {
        const auto& volume = saved[i];
        setVolume(static_cast<uint8_t*>(voice) + 0x18, volume.low, volume.high, volume.value);
    }
    InterlockedIncrement(&restoredCount);
}

bool SupportedImage(HMODULE module) {
    wchar_t path[MAX_PATH];
    const auto length = GetModuleFileNameW(module, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return false;
    const auto file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    const bool sized = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 16 * 1024 * 1024;
    const auto mapping = sized ? CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
    const auto bytes = mapping ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
    constexpr std::array<uint8_t, 32> expected{
        0xeb,0x52,0x64,0xa8,0xd7,0x65,0xb3,0x99,0x7d,0x75,0x88,0x87,0x94,0x68,0xe6,0x4a,
        0xf6,0x56,0x9f,0x3f,0xf1,0x32,0x24,0xe1,0xcd,0x8f,0x9c,0x8d,0x7d,0xae,0x37,0x4b};
    std::array<uint8_t, 32> digest{};
    const bool matches = bytes && BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        static_cast<PUCHAR>(bytes), static_cast<ULONG>(size.QuadPart),
        digest.data(), static_cast<ULONG>(digest.size())) == 0 && digest == expected;
    if (bytes) UnmapViewOfFile(bytes);
    if (mapping) CloseHandle(mapping);
    CloseHandle(file);
    return matches;
}

DWORD WINAPI Install(void*) {
    wchar_t path[MAX_PATH];
    const auto length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!length || length >= ARRAYSIZE(path)) return 0;
    const auto basename = std::wcsrchr(path, L'\\');
    if (!basename || _wcsicmp(basename + 1, L"YY.exe") != 0) return 0;
    HMODULE module = nullptr;
    for (int attempt = 0; !module && attempt < 600; ++attempt) {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"bizchannel.dll", &module))
            Sleep(100);
    }
    if (!module || !SupportedImage(module)) {
        InterlockedExchange(&status, -1);
        return 0;
    }
    auto* base = reinterpret_cast<uint8_t*>(module);
    struct Hook { size_t rva; void* detour; void** original; };
    const Hook hooks[]{
        {0x152a2, reinterpret_cast<void*>(SetVolume), reinterpret_cast<void**>(&setVolume)},
        {0x142f1, reinterpret_cast<void*>(CloseSession), reinterpret_cast<void**>(&closeSession)},
        {0x13eb4, reinterpret_cast<void*>(Playback), reinterpret_cast<void**>(&playback)},
    };
    if (MH_Initialize() == MH_OK) {
        bool ready = true;
        for (const auto& hook : hooks) {
            void* target = base + hook.rva;
            if (MH_CreateHook(target, hook.detour, hook.original) != MH_OK ||
                MH_QueueEnableHook(target) != MH_OK) {
                ready = false;
                break;
            }
        }
        if (ready && MH_ApplyQueued() == MH_OK) {
            InterlockedExchange(&status, 1);
            return 0;
        }
        MH_Uninitialize();
    }
    InterlockedExchange(&status, -2);
    return 0;
}
}

extern "C" LONG WINAPI YYVolumePatchStatus() {
    return InterlockedCompareExchange(&status, 0, 0);
}
extern "C" LONG WINAPI YYVolumePatchRestores() {
    return InterlockedCompareExchange(&restoredCount, 0, 0);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        if (const auto thread = CreateThread(nullptr, 0, Install, nullptr, 0, nullptr))
            CloseHandle(thread);
    }
    return TRUE;
}
