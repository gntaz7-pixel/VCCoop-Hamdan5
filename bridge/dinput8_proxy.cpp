// Bully Co-op Hamdan | DINPUT8 probe (x86) | No game memory modification.
// Minimal side-loading test for a user-owned Bully: Scholarship Edition binary.
// All DirectInput entrypoints forward to the Windows system DLL.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>

static INIT_ONCE g_init_once = INIT_ONCE_STATIC_INIT;
static HMODULE g_system_dinput8 = nullptr;

static void WriteProbeLog(const char* msg) {
    char location[MAX_PATH] = {};
    DWORD used = GetModuleFileNameA(nullptr, location, MAX_PATH);
    if (used == 0 || used >= MAX_PATH) return;
    char* last_separator = location;
    for (char* p = location; *p; ++p) {
        if (*p == '\\' || *p == '/') last_separator = p + 1;
    }
    *last_separator = '\0';
    const char filename[] = "BullyCoop_bridge.log";
    if (static_cast<size_t>(last_separator - location) + sizeof(filename) > MAX_PATH) return;
    lstrcatA(location, filename);
    HANDLE file = CreateFileA(location, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD ignored = 0;
    DWORD length = static_cast<DWORD>(lstrlenA(msg));
    WriteFile(file, msg, length, &ignored, nullptr);
    CloseHandle(file);
}

static BOOL CALLBACK InitRealDInput(PINIT_ONCE, PVOID, PVOID*) {
    char path[MAX_PATH] = {};
    UINT count = GetSystemDirectoryA(path, MAX_PATH);
    if (count == 0 || count >= MAX_PATH - sizeof("\\dinput8.dll")) {
        WriteProbeLog("BullyCoop probe: unable to locate Windows system directory\r\n");
        return TRUE; // InitOnce completed, the export will return failure.
    }
    lstrcatA(path, "\\dinput8.dll");
    g_system_dinput8 = LoadLibraryA(path);
    if (g_system_dinput8)
        WriteProbeLog("BullyCoop probe: DLL active; native system dinput8 loaded\r\n");
    else
        WriteProbeLog("BullyCoop probe: cannot load native system dinput8\r\n");
    return TRUE;
}

static FARPROC GetNativeProc(const char* function_name) {
    InitOnceExecuteOnce(&g_init_once, InitRealDInput, nullptr, nullptr);
    return g_system_dinput8 ? GetProcAddress(g_system_dinput8, function_name) : nullptr;
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version,
                                                REFIID iid, LPVOID* out, LPUNKNOWN outer) {
    typedef HRESULT(WINAPI* NativeFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    FARPROC proc = GetNativeProc("DirectInput8Create");
    if (!proc) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    WriteProbeLog("BullyCoop probe: DirectInput8Create forwarded successfully\r\n");
    return reinterpret_cast<NativeFn>(proc)(instance, version, iid, out, outer);
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    typedef HRESULT(WINAPI* NativeFn)();
    FARPROC proc = GetNativeProc("DllCanUnloadNow");
    return proc ? reinterpret_cast<NativeFn>(proc)() : S_FALSE;
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* out) {
    typedef HRESULT(WINAPI* NativeFn)(REFCLSID, REFIID, LPVOID*);
    FARPROC proc = GetNativeProc("DllGetClassObject");
    return proc ? reinterpret_cast<NativeFn>(proc)(clsid, iid, out) : CLASS_E_CLASSNOTAVAILABLE;
}

extern "C" HRESULT WINAPI DllRegisterServer() {
    typedef HRESULT(WINAPI* NativeFn)();
    FARPROC proc = GetNativeProc("DllRegisterServer");
    return proc ? reinterpret_cast<NativeFn>(proc)() : E_NOTIMPL;
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    typedef HRESULT(WINAPI* NativeFn)();
    FARPROC proc = GetNativeProc("DllUnregisterServer");
    return proc ? reinterpret_cast<NativeFn>(proc)() : E_NOTIMPL;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
