#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static HMODULE LoadOriginal() {
    wchar_t path[MAX_PATH];
    const auto size = GetSystemDirectoryW(path, ARRAYSIZE(path));
    constexpr wchar_t suffix[] = L"\\version.dll";
    if (!size || size + ARRAYSIZE(suffix) > ARRAYSIZE(path)) return nullptr;
    for (unsigned i = 0; i < ARRAYSIZE(suffix); ++i) path[size + i] = suffix[i];
    HMODULE original = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&LoadOriginal), &self);
    return original == self ? nullptr : original;
}
static FARPROC Resolve(const char* name) {
    static const HMODULE original = LoadOriginal();
    return original ? GetProcAddress(original, name) : nullptr;
}
#define PROXY(result,name,signature,arguments) extern "C" result WINAPI Proxy##name signature { \
    const auto original = reinterpret_cast<result (WINAPI*) signature>(Resolve(#name)); \
    if (!original) { SetLastError(ERROR_PROC_NOT_FOUND); return 0; } \
    return original arguments; \
}
PROXY(DWORD,GetFileVersionInfoSizeA,(LPCSTR p, LPDWORD h),(p,h))
PROXY(DWORD,GetFileVersionInfoSizeW,(LPCWSTR p, LPDWORD h),(p,h))
PROXY(BOOL,GetFileVersionInfoA,(LPCSTR p, DWORD h, DWORD n, LPVOID b),(p,h,n,b))
PROXY(BOOL,GetFileVersionInfoW,(LPCWSTR p, DWORD h, DWORD n, LPVOID b),(p,h,n,b))
PROXY(BOOL,VerQueryValueA,(LPCVOID b,LPCSTR s,LPVOID* v,PUINT n),(b,s,v,n))
PROXY(BOOL,VerQueryValueW,(LPCVOID b,LPCWSTR s,LPVOID* v,PUINT n),(b,s,v,n))
