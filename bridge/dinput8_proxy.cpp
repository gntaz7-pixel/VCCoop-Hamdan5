// Bully Co-op Hamdan | v0.16a ISOLATED LOCAL LOOPBACK diagnostics | Bully: Scholarship Edition.
// EXPERIMENT v0.16a: two real processes, isolated protocol v2, logging; no teleport.
// User visually verified v0.13 ONE walking task is attached and NPC walks a little before stopping.
// v0.16a untested in game; F10 OFF stops new tasks only. No Internet multiplayer.
// This is NOT a verified co-op mod. Never test in your only game installation.
// This DLL is a DirectInput 8 proxy: it forwards actual input calls to the system DLL.
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <unknwn.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <atomic>
#include "safe_walk_planner.h"
#include "loopback_wire.h"
#pragma comment(lib, "Ws2_32.lib")

static INIT_ONCE g_input_init = INIT_ONCE_STATIC_INIT;
static INIT_ONCE g_probe_init = INIT_ONCE_STATIC_INIT;
// Explicit, local-host-only opt-in for two game windows on ONE computer.
// Inactive by default, NEVER enabled for a remote LAN host.
static std::atomic<bool> g_allowBackgroundHostAutoWalk{false};
static HMODULE g_system_dinput8 = nullptr;
static const DWORD kExpectedBullySize = 8204288; // User's Bully.exe, SHA256 below in README.
static const uintptr_t kImageBase = 0x400000;
static const uintptr_t kPlayerGetPosRva = 0x1D2670;
// 0x5C7380 (called by native PlayerGetPosXYZ at 0x5D267F)
// returns the script-managed player actor directly from [0x00C1AEA8]
// for entity category 3. v0.4 incorrectly read the *separate* player manager.
static const uintptr_t kPlayerActorGlobalRva = 0x81AEA8; // VA 0x00C1AEA8 - image base.
static const char kPlayerGetPosSignature[] = "\x83\xec\x0c\x56\x51\x8b\xc4\x6a\x00\xc7\x00\x03\x00\x00\x00";
// Static analysis of this exact Bully.exe: PedCreateXYZ's registration table
// points at script native wrapper 0x5CFCB0, which invokes the lower-level
// routine at 0x5CF810. Its full contract is NOT publicly established;
// do not invoke without separate user opt-in and a clean game backup.
static const uintptr_t kPedCreateLowLevelRva = 0x1CF810;  // VA 0x005CF810
static const uintptr_t kPedCreateRegistryRva = 0x6EA638;  // 0x006EA638 table entry
static const uintptr_t kPedCreateNameRva = 0x528810;       // VA 0x00928810
static const uintptr_t kPedCreateWrapperVa = 0x005CFCB0;
static const unsigned char kPedCreateCodeSignature[] = {
    0x8B, 0x0D, 0x08, 0xC1, 0xC2, 0x00, 0x83, 0xEC, 0x10
};
// Confirmed by offline disassembly of the same user-provided Bully.exe:
// PedSetPosXYZ script wrapper at 0x005C83B0 -> ped lookup(2,handle) at
// 0x005C7380 -> scene position helper at 0x005C7600(ped,&vec3).
// Invoke ONLY on the game's window/message thread, never the UDP thread.
static const uintptr_t kPedSetPosRegistryRva = 0x6EA6E8;
static const uintptr_t kPedSetPosNameRva = 0x5286A8;
static const uintptr_t kPedSetPosWrapperVa = 0x005C83B0;
static const uintptr_t kPedLookupRva = 0x1C7380;
static const uintptr_t kPedSetPosLowLevelRva = 0x1C7600;
static const unsigned char kPedLookupCodeSignature[] = {
    0x57, 0x8B, 0x7C, 0x24, 0x08, 0x83, 0xFF, 0xFF, 0x75, 0x04, 0x33, 0xC0, 0x5F, 0xC3
};
static const unsigned char kPedSetPosCodeSignature[] = {
    0x83, 0xEC, 0x0C, 0x56, 0x8B, 0x74, 0x24, 0x14, 0x85, 0xF6, 0x57
};


// v0.13 offline discovery in THIS exact Bully.exe. PedMoveToXYZ Lua wrapper
// at VA 0x005C7DF0 allocates task 0x34 via 0x005EEAA0, constructs a
// pedestrian walk task at 0x004705B0 and registers it at ped+0x5F0 with
// 0x00471390. This uses the engine task stack, NOT PedSetPosXYZ teleport.
// Calling contracts are inferred from machine code, not verified in gameplay.
static constexpr uintptr_t kWalkNameRva = 0x5286D8;
static constexpr uintptr_t kWalkEntryRva = 0x6EA6D0;
static constexpr uintptr_t kWalkScriptVa = 0x005C7DF0;
static constexpr uintptr_t kWalkTaskAllocatorRva = 0x1EEAA0; // VA 0x5EEAA0
static constexpr uintptr_t kWalkTaskConstructorRva = 0x705B0; // VA 0x4705B0
static constexpr uintptr_t kWalkTaskAttachRva = 0x71390; // VA 0x471390
static bool ValidateWalkTaskFunction(uintptr_t base);

static void WriteProbeLog(const char* msg) {
    char path[MAX_PATH] = {};
    const DWORD used = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (used == 0 || used >= MAX_PATH) return;
    char* end = path;
    for (char* it = path; *it; ++it) {
        if (*it == '\\' || *it == '/') end = it + 1;
    }
    *end = '\0';
    const char name[] = "BullyCoop_bridge.log";
    if (static_cast<size_t>(end - path) + sizeof(name) > MAX_PATH) return;
    lstrcatA(path, name);
    HANDLE log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return;
    DWORD ignored = 0;
    WriteFile(log, msg, static_cast<DWORD>(lstrlenA(msg)), &ignored, nullptr);
    CloseHandle(log);
}

static BOOL CALLBACK InitSystemInput(PINIT_ONCE, PVOID, PVOID*) {
    char path[MAX_PATH] = {};
    const UINT count = GetSystemDirectoryA(path, MAX_PATH);
    if (count == 0 || count > MAX_PATH - sizeof("\\dinput8.dll")) {
        WriteProbeLog("BullyCoop probe: unable to locate native system dinput8\r\n");
        return TRUE;
    }
    lstrcatA(path, "\\dinput8.dll");
    g_system_dinput8 = LoadLibraryA(path);
    WriteProbeLog(g_system_dinput8 ?
        "BullyCoop probe: DLL active; native system dinput8 loaded\r\n" :
        "BullyCoop probe: cannot load native system dinput8\r\n");
    return TRUE;
}

static FARPROC GetNativeProc(const char* name) {
    InitOnceExecuteOnce(&g_input_init, InitSystemInput, nullptr, nullptr);
    return g_system_dinput8 ? GetProcAddress(g_system_dinput8, name) : nullptr;
}

// Read memory through the Windows API: invalid addresses result in false, not a direct dereference.
template <typename T>
static bool ReadAt(uintptr_t address, T* value) {
    SIZE_T read = 0;
    if (address < 0x10000 || !value) return false;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                             value, sizeof(T), &read) != 0 && read == sizeof(T);
}

struct Position { float x, y, z; };

// Only verifies the known registration and code bytes; never spawns by itself.
static bool ValidatePedCreationFunction(uintptr_t base) {
    struct NativeEntry { uint32_t name; uint32_t address; } entry = {};
    unsigned char code[sizeof(kPedCreateCodeSignature)] = {};
    char name[13] = {};
    if (!ReadAt(base + kPedCreateRegistryRva, &entry) ||
        !ReadAt(base + kPedCreateLowLevelRva, &code) ||
        !ReadAt(base + kPedCreateNameRva, &name)) return false;
    return entry.name == base + kPedCreateNameRva &&
           entry.address == kPedCreateWrapperVa &&
           std::memcmp(code, kPedCreateCodeSignature, sizeof(code)) == 0 &&
           std::strcmp(name, "PedCreateXYZ") == 0;
}

static bool ValidatePedMovementFunction(uintptr_t base) {
    struct NativeEntry { uint32_t name; uint32_t address; } entry = {};
    unsigned char lookup[sizeof(kPedLookupCodeSignature)] = {};
    unsigned char setPos[sizeof(kPedSetPosCodeSignature)] = {};
    char name[13] = {};
    if (!ReadAt(base + kPedSetPosRegistryRva, &entry) ||
        !ReadAt(base + kPedSetPosNameRva, &name) ||
        !ReadAt(base + kPedLookupRva, &lookup) ||
        !ReadAt(base + kPedSetPosLowLevelRva, &setPos)) return false;
    return entry.name == base + kPedSetPosNameRva &&
           entry.address == kPedSetPosWrapperVa &&
           std::strcmp(name, "PedSetPosXYZ") == 0 &&
           std::memcmp(lookup, kPedLookupCodeSignature, sizeof(lookup)) == 0 &&
           std::memcmp(setPos, kPedSetPosCodeSignature, sizeof(setPos)) == 0;
}

static bool ValidateWalkTaskFunction(uintptr_t base) {
    struct NativeEntry { uint32_t name; uint32_t address; } entry = {};
    char name[13] = {};
    unsigned char wrapper[7] = {}, alloc[5] = {}, ctor[10] = {}, attach[6] = {};
    const unsigned char wrapperExpected[7] = {0x83,0xEC,0x1C,0x56,0x8B,0x74,0x24};
    const unsigned char allocExpected[5] = {0xE9,0x8B,0xFD,0xFF,0xFF};
    const unsigned char ctorExpected[10] = {0xD9,0x44,0x24,0x0C,0x8A,0x54,0x24,0x18,0x8B,0xC1};
    const unsigned char attachExpected[6] = {0x53,0x55,0x56,0x8B,0xF1,0x8B};
    if (!ReadAt(base+kWalkEntryRva, &entry) || !ReadAt(base+kWalkNameRva, &name) ||
        !ReadAt(base+(kWalkScriptVa-kImageBase), &wrapper) ||
        !ReadAt(base+kWalkTaskAllocatorRva, &alloc) ||
        !ReadAt(base+kWalkTaskConstructorRva, &ctor) ||
        !ReadAt(base+kWalkTaskAttachRva, &attach)) return false;
    return entry.name == base+kWalkNameRva && entry.address == kWalkScriptVa &&
           std::strcmp(name, "PedMoveToXYZ") == 0 &&
           std::memcmp(wrapper,wrapperExpected,sizeof(wrapper)) == 0 &&
           std::memcmp(alloc,allocExpected,sizeof(alloc)) == 0 &&
           std::memcmp(ctor,ctorExpected,sizeof(ctor)) == 0 &&
           std::memcmp(attach,attachExpected,sizeof(attach)) == 0;
}

static bool VersionMatches(uintptr_t base) {
    if (base != kImageBase) {
        WriteProbeLog("BullyCoop position: unsupported relocated Bully.exe; disabled\r\n");
        return false;
    }
    char exe[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH)) return false;
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!GetFileAttributesExA(exe, GetFileExInfoStandard, &attributes)
        || attributes.nFileSizeHigh || attributes.nFileSizeLow != kExpectedBullySize) {
        WriteProbeLog("BullyCoop position: Bully.exe file size differs; disabled\r\n");
        return false;
    }
    char observed[sizeof(kPlayerGetPosSignature) - 1] = {};
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(base + kPlayerGetPosRva), observed,
        sizeof(observed), &read) || read != sizeof(observed)
        || memcmp(observed, kPlayerGetPosSignature, sizeof(observed)) != 0) {
        WriteProbeLog("BullyCoop position: PlayerGetPosXYZ code signature differs; disabled\r\n");
        return false;
    }
    return true;
}

// This is the character-pointer path used by the game native PlayerGetPosXYZ:
//   PlayerGetPosXYZ -> 0x5C7380(type=3) -> mov eax,[0xC1AEA8]
//   actor->0x1554 / actor->0x14 -> transform->0x30 (or actor+0x04).
// v0.4 read 0xD02850+0x6B84 as if it were the actor lookup; this is a
// DIFFERENT manager used later by PlayerGetPosXYZ and is often uninitialized.
// Read-only, with explicit failure reasons for on-device validation.
enum class PositionStatus {
    Good, GlobalEmpty, GlobalUnreadable, ActorInvalid, NestedUnreadable,
    TransformUnreadable, CoordinatesUnreadable, CoordinatesImplausible
};

static const char* StatusName(PositionStatus status) {
    switch (status) {
        case PositionStatus::Good: return "ready";
        case PositionStatus::GlobalEmpty: return "player actor global is empty (menu/loading?)";
        case PositionStatus::GlobalUnreadable: return "unable to read actor global";
        case PositionStatus::ActorInvalid: return "actor pointer looks invalid";
        case PositionStatus::NestedUnreadable: return "unable to read actor nested pointer";
        case PositionStatus::TransformUnreadable: return "unable to read transform pointer";
        case PositionStatus::CoordinatesUnreadable: return "unable to read coordinates";
        case PositionStatus::CoordinatesImplausible: return "coordinates are not finite/in expected range";
    }
    return "unknown";
}

static PositionStatus ReadJimmyPosition(uintptr_t base, Position* out) {
    if (!out) return PositionStatus::CoordinatesUnreadable;
    uint32_t actor = 0;
    if (!ReadAt(base + kPlayerActorGlobalRva, &actor)) return PositionStatus::GlobalUnreadable;
    if (!actor) return PositionStatus::GlobalEmpty;
    if (actor < 0x10000u || actor > 0x7FFFFFFFu) return PositionStatus::ActorInvalid;

    uint32_t nested = 0;
    if (!ReadAt(static_cast<uintptr_t>(actor) + 0x1554u, &nested)) return PositionStatus::NestedUnreadable;
    uint32_t transform = 0;
    uintptr_t address = 0;
    if (nested != 0) {
        if (!ReadAt(static_cast<uintptr_t>(nested) + 0x14u, &transform))
            return PositionStatus::TransformUnreadable;
        address = transform ? static_cast<uintptr_t>(transform) + 0x30u :
                              static_cast<uintptr_t>(nested) + 0x04u;
    } else {
        if (!ReadAt(static_cast<uintptr_t>(actor) + 0x14u, &transform))
            return PositionStatus::TransformUnreadable;
        address = transform ? static_cast<uintptr_t>(transform) + 0x30u :
                              static_cast<uintptr_t>(actor) + 0x04u;
    }
    if (!ReadAt(address, out)) return PositionStatus::CoordinatesUnreadable;
    if (!std::isfinite(out->x) || !std::isfinite(out->y) || !std::isfinite(out->z) ||
        std::fabs(out->x) >= 1000000.0f || std::fabs(out->y) >= 1000000.0f ||
        std::fabs(out->z) >= 1000000.0f)
        return PositionStatus::CoordinatesImplausible;
    return PositionStatus::Good;
}


// LAN-only position transport, v0.6.
// Shares positions between two game processes; does NOT instantiate a remote character.
// All network access is disabled by default and requires explicit BullyCoop.ini settings.
// Protocol is intentionally unencrypted; use on trusted private LAN/VPN only.
static const uint32_t kNetworkMagic = BullyLoopback::kNetworkMagic; // "BCOP"
static const uint16_t kNetworkVersion = BullyLoopback::kNetworkVersion; // v2: stale fake guest v1 rejected
static const DWORD kRemoteTimeoutMs = 3000;
static const DWORD kPeerResetMs = 10000;

#pragma pack(push, 1)
struct PositionPacket {
    uint32_t magic;
    uint16_t version;
    uint16_t senderRole; // 1 = host, 2 = guest
    uint32_t sessionCode;
    uint32_t sequence;
    float x;
    float y;
    float z;
};
#pragma pack(pop)
static_assert(sizeof(PositionPacket) == 28, "Position packet format changed");

struct NetworkProbe {
    SOCKET socket = INVALID_SOCKET;
    bool winsockStarted = false;
    bool enabled = false;
    bool host = false;
    bool peerKnown = false;
    sockaddr_in peer = {};
    uint32_t sessionCode = 0;
    uint32_t nextSequence = 0;
    uint32_t lastSequence = 0;
    bool haveSequence = false;
    bool haveRemote = false;
    Position remote = {};
    DWORD lastReceived = 0;
    DWORD lastRemoteLog = 0;
    DWORD initializedAt = 0;
    DWORD lastNoPeerLog = 0;
    bool previouslyConnected = false;
    DWORD lastRejectLog = 0;
    uint32_t ignoredLegacy = 0;
    uint32_t ignoredWrongCode = 0;
    uint32_t ignoredOther = 0;
};

static void ReadGameDirectoryFile(const char* filename, char out[MAX_PATH]) {
    out[0] = '\0';
    const DWORD used = GetModuleFileNameA(nullptr, out, MAX_PATH);
    if (!used || used >= MAX_PATH) { out[0] = '\0'; return; }
    char* cursor = out;
    for (char* it = out; *it; ++it)
        if (*it == '\\' || *it == '/') cursor = it + 1;
    *cursor = '\0';
    if (static_cast<size_t>(cursor - out) + strlen(filename) + 1 > MAX_PATH) {
        out[0] = '\0';
        return;
    }
    lstrcatA(out, filename);
}

static bool InitNetwork(NetworkProbe* net) {
    if (!net) return false;
    char ini[MAX_PATH] = {};
    ReadGameDirectoryFile("BullyCoop.ini", ini);
    if (!ini[0] || GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        WriteProbeLog("BullyCoop net: inactive (BullyCoop.ini absent)\r\n");
        return false;
    }
    if (GetPrivateProfileIntA("Network", "Enabled", 0, ini) != 1) {
        WriteProbeLog("BullyCoop net: inactive (Enabled is not 1)\r\n");
        return false;
    }
    char role[16] = {};
    GetPrivateProfileStringA("Network", "Role", "", role, sizeof(role), ini);
    if (lstrcmpiA(role, "host") == 0) net->host = true;
    else if (lstrcmpiA(role, "guest") == 0) net->host = false;
    else {
        WriteProbeLog("BullyCoop net: Role must be host or guest; disabled\r\n");
        return false;
    }
    // Bind a loopback-enabled host ONLY to 127.0.0.1; it cannot listen on LAN.
    // The guest must send to HostAddress=127.0.0.1 in its separate game directory.
    const bool loopbackHost = net->host &&
        GetPrivateProfileIntA("Experimental", "AllowBackgroundHostWalk", 0, ini) == 1;
    const UINT port = GetPrivateProfileIntA("Network", "Port", 7791, ini);
    const UINT pin = GetPrivateProfileIntA("Network", "SessionCode", 0, ini);
    if (port < 1024 || port > 65535 || pin < 100000 || pin > 999999999u) {
        WriteProbeLog("BullyCoop net: invalid port or session code; disabled\r\n");
        return false;
    }
    net->sessionCode = pin;
    WSADATA wsadata = {};
    if (WSAStartup(MAKEWORD(2,2), &wsadata) != 0) {
        WriteProbeLog("BullyCoop net: WSAStartup failed\r\n");
        return false;
    }
    net->winsockStarted = true;
    net->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (net->socket == INVALID_SOCKET) {
        WriteProbeLog("BullyCoop net: socket creation failed\r\n");
        WSACleanup(); net->winsockStarted = false;
        return false;
    }
    u_long nonBlocking = 1;
    if (ioctlsocket(net->socket, FIONBIO, &nonBlocking) != 0) {
        WriteProbeLog("BullyCoop net: nonblocking socket failed\r\n");
        closesocket(net->socket); net->socket = INVALID_SOCKET;
        WSACleanup(); net->winsockStarted = false;
        return false;
    }
    sockaddr_in bindAddress = {};
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_port = htons(static_cast<u_short>(net->host ? port : 0));
    bindAddress.sin_addr.s_addr = htonl(loopbackHost ? INADDR_LOOPBACK : INADDR_ANY);
    if (bind(net->socket, reinterpret_cast<const sockaddr*>(&bindAddress), sizeof(bindAddress)) != 0) {
        WriteProbeLog("BullyCoop net: UDP bind failed (is the port already in use?)\r\n");
        closesocket(net->socket); net->socket = INVALID_SOCKET;
        WSACleanup(); net->winsockStarted = false;
        return false;
    }
    if (!net->host) {
        char address[64] = {};
        GetPrivateProfileStringA("Network", "HostAddress", "", address, sizeof(address), ini);
        net->peer.sin_family = AF_INET;
        net->peer.sin_port = htons(static_cast<u_short>(port));
        if (InetPtonA(AF_INET, address, &net->peer.sin_addr) != 1 ||
            net->peer.sin_addr.s_addr == INADDR_ANY) {
            WriteProbeLog("BullyCoop net: invalid HostAddress IPv4; disabled\r\n");
            closesocket(net->socket); net->socket = INVALID_SOCKET;
            WSACleanup(); net->winsockStarted = false;
            return false;
        }
        net->peerKnown = true;
    }
    net->enabled = true;
    // Explicitly log REAL game process settings to expose wrong .ini copies.
    {
        char message[512] = {};
        char peerIp[INET_ADDRSTRLEN] = "-";
        if (!net->host) {
            InetNtopA(AF_INET, &net->peer.sin_addr, peerIp, sizeof(peerIp));
        }
        sprintf_s(message, sizeof(message),
            "BullyCoop v0.16a: NET CONFIG pid=%lu role=%s port=%u code=%u peer=%s protocol=%u loopback=%d ini=%s\r\n",
            static_cast<unsigned long>(GetCurrentProcessId()), net->host ? "host" : "guest",
            port, pin, peerIp, kNetworkVersion, loopbackHost ? 1 : 0, ini);
        WriteProbeLog(message);
    }
    // Only mark background movement after successfully binding the HOST socket
    // to the loopback interface. Other cases retain foreground-only dispatch.
    if (loopbackHost) {
        g_allowBackgroundHostAutoWalk.store(true);
        WriteProbeLog("BullyCoop v0.16a: LOCAL LOOPBACK HOST only; background AUTO walking enabled (opt-in)\r\n");
    }
    net->initializedAt = GetTickCount();
    WriteProbeLog(net->host ?
        "BullyCoop net v0.6: HOST UDP ready (read-only position exchange)\r\n" :
        "BullyCoop net v0.6: GUEST UDP ready (read-only position exchange)\r\n");
    return true;
}

static bool ValidNetworkPosition(const Position& pos) {
    return std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z) &&
           std::fabs(pos.x) < 1000000.0f && std::fabs(pos.y) < 1000000.0f &&
           std::fabs(pos.z) < 1000000.0f;
}

static void SendNetworkPosition(NetworkProbe* net, const Position& local) {
    if (!net->enabled || !net->peerKnown || !ValidNetworkPosition(local)) return;
    PositionPacket packet = {};
    packet.magic = htonl(kNetworkMagic);
    packet.version = htons(kNetworkVersion);
    packet.senderRole = htons(net->host ? 1 : 2);
    packet.sessionCode = htonl(net->sessionCode);
    packet.sequence = htonl(++net->nextSequence);
    packet.x = local.x;
    packet.y = local.y;
    packet.z = local.z;
    const int result = sendto(net->socket, reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
            reinterpret_cast<const sockaddr*>(&net->peer), sizeof(net->peer));
    if (result == SOCKET_ERROR) {
        // UDP network errors are not fatal; subsequent updates may succeed.
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAENETUNREACH && error != WSAEHOSTUNREACH) {
            // Avoid writing every 100 ms when a firewall or network blocks packets.
            static DWORD lastErrorLog = 0;
            const DWORD tick = GetTickCount();
            if (tick - lastErrorLog >= 15000) {
                WriteProbeLog("BullyCoop net: position send failed (retrying)\r\n");
                lastErrorLog = tick;
            }
        }
    }
}

static void ReceiveNetworkPositions(NetworkProbe* net, DWORD tick) {
    if (!net->enabled) return;
    if (net->host && net->peerKnown && tick - net->lastReceived > kPeerResetMs) {
        net->peerKnown = false;
        net->haveSequence = false;
        net->haveRemote = false;
    }
    // Nonblocking receive, bounded to protect the game's process against UDP packet floods.
    for (int i = 0; i < 16; ++i) {
        PositionPacket packet = {};
        sockaddr_in sender = {};
        int senderLength = sizeof(sender);
        const int count = recvfrom(net->socket, reinterpret_cast<char*>(&packet), sizeof(packet), 0,
                                   reinterpret_cast<sockaddr*>(&sender), &senderLength);
        if (count == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK && tick - net->lastRejectLog >= 15000) {
                char errorLine[140] = {};
                sprintf_s(errorLine, sizeof(errorLine),
                    "BullyCoop v0.16a: UDP RX error=%d (Windows; retrying)\r\n", error);
                WriteProbeLog(errorLine);
                net->lastRejectLog = tick;
            }
            break;
        }
        if (count != sizeof(packet) || sender.sin_family != AF_INET ||
            ntohl(packet.magic) != kNetworkMagic) {
            ++net->ignoredOther;
            continue;
        }
        if (ntohs(packet.version) != kNetworkVersion) {
            ++net->ignoredLegacy;
            continue;
        }
        if (ntohl(packet.sessionCode) != net->sessionCode) {
            ++net->ignoredWrongCode;
            continue;
        }
        if (!BullyLoopback::ValidEnvelope(ntohl(packet.magic), ntohs(packet.version),
              ntohs(packet.senderRole), ntohl(packet.sessionCode), net->sessionCode,
              net->host)) {
            ++net->ignoredOther;
            continue;
        }
        Position pos = {packet.x, packet.y, packet.z};
        if (!ValidNetworkPosition(pos)) continue;
        if (net->peerKnown) {
            if (sender.sin_addr.s_addr != net->peer.sin_addr.s_addr ||
                sender.sin_port != net->peer.sin_port) {
                ++net->ignoredOther;
                continue;
            }
        } else if (net->host) {
            net->peer = sender;
            net->peerKnown = true;
            net->haveSequence = false;
        } else continue;
        const uint32_t seq = ntohl(packet.sequence);
        if (net->haveSequence && static_cast<int32_t>(seq - net->lastSequence) <= 0) continue;
        net->haveSequence = true;
        net->lastSequence = seq;
        net->remote = pos;
        net->haveRemote = true;
        net->lastReceived = tick;
        if (!net->previouslyConnected) {
            char peerIp[INET_ADDRSTRLEN] = {};
            InetNtopA(AF_INET, &sender.sin_addr, peerIp, sizeof(peerIp));
            char peerLine[192] = {};
            sprintf_s(peerLine, sizeof(peerLine),
                "BullyCoop v0.16a: PEER VERIFIED role=%s from=%s:%u protocol=%u\r\n",
                net->host ? "guest" : "host", peerIp,
                static_cast<unsigned>(ntohs(sender.sin_port)), kNetworkVersion);
            WriteProbeLog(peerLine);
            WriteProbeLog("BullyCoop net: first remote player position RECEIVED\r\n");
            net->previouslyConnected = true;
        }
    }
    if ((net->ignoredLegacy || net->ignoredWrongCode || net->ignoredOther) &&
        tick - net->lastRejectLog >= 15000) {
        char rejects[240] = {};
        sprintf_s(rejects, sizeof(rejects),
            "BullyCoop v0.16a: RX IGNORED legacy_v1=%u wrong_code=%u other=%u\r\n",
            net->ignoredLegacy, net->ignoredWrongCode, net->ignoredOther);
        WriteProbeLog(rejects);
        net->ignoredLegacy = net->ignoredWrongCode = net->ignoredOther = 0;
        net->lastRejectLog = tick;
    }
    if (net->haveRemote && tick - net->lastReceived > kRemoteTimeoutMs) {
        net->haveRemote = false;
        net->previouslyConnected = false;
        WriteProbeLog("BullyCoop net: remote position timed out\r\n");
    }
    if (!net->haveRemote && tick - net->initializedAt >= 7000 &&
        tick - net->lastNoPeerLog >= 15000) {
        WriteProbeLog("BullyCoop net: waiting for remote player; check both game sessions, firewall, IP, port and SessionCode\r\n");
        net->lastNoPeerLog = tick;
    }
    if (net->haveRemote && tick - net->lastRemoteLog >= 2000) {
        char text[180] = {};
        sprintf_s(text, sizeof(text),
                  "BullyCoop remote player: x=%.2f y=%.2f z=%.2f age_ms=%lu\r\n",
                  net->remote.x, net->remote.y, net->remote.z,
                  static_cast<unsigned long>(tick - net->lastReceived));
        WriteProbeLog(text);
        net->lastRemoteLog = tick;
    }
}

// NPC TEST ONLY. This is not remote-character sync and may fail on this game build.
// It is gated by BOTH ini option and a fresh F9 press after live game loading.
// The game's script function must run on the game's window thread, NOT on our
// UDP reader thread. WH_GETMESSAGE provides this limited test path.
// No game memory patch is installed and none of the existing peds is replaced.
static constexpr UINT kModelForProbe = 0; // Assumed model ID; exact appearance not verified.
static std::atomic<bool> g_spawnAttempted{false};
static HHOOK g_gameMessageHook = nullptr;
static HWND g_gameWindow = nullptr;
static UINT g_spawnMessage = 0;
static std::atomic<int> g_npcHandle{-1};
static std::atomic<bool> g_followEnabled{false}; // F11 READ-ONLY identity log
static std::atomic<bool> g_autoWalkEnabled{false}; // F10 toggles NEW walking-task dispatches
static DWORD g_walkTaskIntervalMs = 1300; // ms; bounded 900..2800
// Max target lead in CENTIMETERS OF GAME XY COORDINATES. 0 disables prediction.
// Only extrapolate when consecutive fresh network samples establish a plausible velocity.
static DWORD g_walkLeadCm = 180; // default 1.8 coordinate units; bounded 0..240
static bool g_movementProbeAllowed = false;
static bool g_movementNativeVerified = false;
static DWORD g_lastFollowLog = 0;
// Once we create a NPC, keep a fixed flat-ground safety anchor for this test session.
// This is not pathfinding, ground detection, or reliable live remote-player movement.
static std::atomic<bool> g_movePostPending{false};
// Prevent scripted position steps into Jimmy's collision/interaction radius.
// These are TEST heuristics, not a collision engine or walk-path solution.
// Only the UDP thread writes this mailbox. Window thread reads a snapshot.
static SRWLOCK g_remoteLock = SRWLOCK_INIT;
static Position g_remoteSnapshot = {};
static DWORD g_remoteSnapshotTick = 0;
static bool g_remoteSnapshotValid = false;
static Position g_remoteVelocity = {}; // network worker estimates velocity; read under g_remoteLock
static Position g_remotePriorPosition = {};
static DWORD g_remotePriorTick = 0;
static bool g_remoteVelocityValid = false;

static void PublishRemoteSnapshot(const NetworkProbe& network) {
    AcquireSRWLockExclusive(&g_remoteLock);
    if (!network.haveRemote) {
        // A disconnected client must NEVER keep the last movement direction alive.
        g_remotePriorTick = 0;
        g_remoteVelocityValid = false;
        g_remoteVelocity = {};
    } else if (network.lastReceived != g_remotePriorTick) {
        if (g_remotePriorTick) {
            const DWORD elapsed = network.lastReceived - g_remotePriorTick;
            if (elapsed >= 60 && elapsed <= 450) {
                const float seconds = elapsed * 0.001f;
                Position observed = {(network.remote.x-g_remotePriorPosition.x)/seconds,
                    (network.remote.y-g_remotePriorPosition.y)/seconds, 0.0f};
                const float speedSq = observed.x*observed.x + observed.y*observed.y;
                // Reject 5+ units/sec: fast host moves, teleports and packet jumps
                // must not be projected into an NPC walking goal.
                if (std::isfinite(speedSq) && speedSq >= 0.01f && speedSq <= 25.0f) {
                    // Smooth 10Hz sender jitter; reset velocity immediately if stationary.
                    if (g_remoteVelocityValid) {
                        observed.x = g_remoteVelocity.x*0.55f + observed.x*0.45f;
                        observed.y = g_remoteVelocity.y*0.55f + observed.y*0.45f;
                    }
                    g_remoteVelocity = observed;
                    g_remoteVelocityValid = true;
                } else {
                    g_remoteVelocityValid = false;
                    g_remoteVelocity = {};
                }
            } else {
                g_remoteVelocityValid = false;
                g_remoteVelocity = {};
            }
        }
        g_remotePriorPosition = network.remote;
        g_remotePriorTick = network.lastReceived;
    }
    g_remoteSnapshot = network.remote;
    g_remoteSnapshotTick = network.lastReceived;
    g_remoteSnapshotValid = network.haveRemote;
    ReleaseSRWLockExclusive(&g_remoteLock);
}

// For safety, read the NPC's OWN transform, do not assume its Z equals Jimmy's.
// Actor transform layout is validated for Jimmy in v0.5; NPC layout/behavior
// remains experimental and the call is protected by ReadProcessMemory checks.
static bool ReadPedWorldPosition(void* ped, Position* out) {
    if (!ped || !out) return false;
    const uintptr_t actor = reinterpret_cast<uintptr_t>(ped);
    if (actor < 0x10000u || actor > 0x7fffffffu) return false;
    uint32_t nested = 0;
    if (!ReadAt(actor + 0x1554u, &nested)) return false;
    uint32_t transform = 0;
    uintptr_t address = 0;
    if (nested) {
        if (!ReadAt(static_cast<uintptr_t>(nested) + 0x14u, &transform)) return false;
        address = transform ? static_cast<uintptr_t>(transform) + 0x30u :
                              static_cast<uintptr_t>(nested) + 0x04u;
    } else {
        if (!ReadAt(actor + 0x14u, &transform)) return false;
        address = transform ? static_cast<uintptr_t>(transform) + 0x30u : actor + 0x04u;
    }
    return ReadAt(address, out) && ValidNetworkPosition(*out);
}

// F11 identity diagnostics stay READ ONLY; F10 issues carefully spaced engine walk tasks.
// Prior teleport versions disrupted Jimmy; never use PedSetPosXYZ here.
static void RunNpcIdentityProbe() {
    if (!g_followEnabled.load() || !g_movementProbeAllowed) return;
    const DWORD tick = GetTickCount();
    if (g_lastFollowLog && tick - g_lastFollowLog < 1000) return;
    g_lastFollowLog = tick;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const int handle = g_npcHandle.load();
    uint32_t playerActor = 0;
    if (handle <= 0 || !ReadAt(base + kPlayerActorGlobalRva, &playerActor) || !playerActor) {
        WriteProbeLog("BullyCoop v0.12: ID CHECK waiting for NPC/player actor\r\n");
        return;
    }
    typedef void* (__cdecl* LookupPedFn)(int handle, int actorType);
    LookupPedFn lookup = reinterpret_cast<LookupPedFn>(base + kPedLookupRva);
    uintptr_t npcActor = 0;
    __try {
        npcActor = reinterpret_cast<uintptr_t>(lookup(handle, 2));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteProbeLog("BullyCoop v0.12: ID CHECK lookup exception; READ ONLY stopped\r\n");
        g_followEnabled.store(false);
        return;
    }
    if (!npcActor || npcActor < 0x10000u || npcActor > 0x7fffffffu) {
        WriteProbeLog("BullyCoop v0.12: ID CHECK no valid NPC pointer; READ ONLY stopped\r\n");
        g_followEnabled.store(false);
        return;
    }
    uint32_t playerNested = 0, npcNested = 0;
    uint32_t playerTransform = 0, npcTransform = 0;
    ReadAt(static_cast<uintptr_t>(playerActor) + 0x1554u, &playerNested);
    ReadAt(npcActor + 0x1554u, &npcNested);
    if (playerNested) ReadAt(static_cast<uintptr_t>(playerNested) + 0x14u, &playerTransform);
    else ReadAt(static_cast<uintptr_t>(playerActor) + 0x14u, &playerTransform);
    if (npcNested) ReadAt(static_cast<uintptr_t>(npcNested) + 0x14u, &npcTransform);
    else ReadAt(npcActor + 0x14u, &npcTransform);

    const bool sameActor = (npcActor == static_cast<uintptr_t>(playerActor));
    const bool sameNested = playerNested && npcNested && playerNested == npcNested;
    const bool sameTransform = playerTransform && npcTransform && playerTransform == npcTransform;
    char line[400] = {};
    sprintf_s(line, sizeof(line),
        "BullyCoop v0.12: ID POINTERS handle=%d player=%p npc=%p playerNested=%p npcNested=%p playerTransform=%p npcTransform=%p\r\n",
        handle,
        reinterpret_cast<void*>(static_cast<uintptr_t>(playerActor)),
        reinterpret_cast<void*>(npcActor),
        reinterpret_cast<void*>(static_cast<uintptr_t>(playerNested)),
        reinterpret_cast<void*>(static_cast<uintptr_t>(npcNested)),
        reinterpret_cast<void*>(static_cast<uintptr_t>(playerTransform)),
        reinterpret_cast<void*>(static_cast<uintptr_t>(npcTransform)));
    WriteProbeLog(line);
    Position jimmy = {}, npc = {};
    const bool playerValid = ReadJimmyPosition(base, &jimmy) == PositionStatus::Good;
    const bool npcValid = ReadPedWorldPosition(reinterpret_cast<void*>(npcActor), &npc);
    if (playerValid && npcValid) {
        const float dx = jimmy.x - npc.x;
        const float dy = jimmy.y - npc.y;
        sprintf_s(line, sizeof(line),
            "BullyCoop v0.12: ID POS Jimmy=%.2f,%.2f,%.2f NPC=%.2f,%.2f,%.2f gapXY=%.2f (NO MOVEMENT)\r\n",
            jimmy.x, jimmy.y, jimmy.z, npc.x, npc.y, npc.z,
            std::sqrt(dx*dx + dy*dy));
        WriteProbeLog(line);
    } else {
        WriteProbeLog("BullyCoop v0.12: ID POS unable to read one actor position; NO MOVEMENT\r\n");
    }
    if (sameActor || sameNested || sameTransform) {
        WriteProbeLog("BullyCoop v0.12: DANGER shared actor/transform with Jimmy; READ ONLY stopped. Do NOT use v0.11 movement.\r\n");
        g_followEnabled.store(false);
    }
}

// ONE engine walk instruction, called only from the game window thread. NO teleport.
// This is an opt-in test on a BACKUP installation. Do not save the game.
static void RunNpcSingleWalkProbe() {
    if (!g_movementProbeAllowed || !g_movementNativeVerified) return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!VersionMatches(base) || !ValidateWalkTaskFunction(base)) {
        WriteProbeLog("BullyCoop v0.16a: WALK BLOCKED: native code mismatch\r\n");
        return;
    }
    Position remote = {};
    DWORD received = 0;
    bool haveRemote = false;
    bool velocityValid = false;
    Position velocity = {};
    AcquireSRWLockShared(&g_remoteLock);
    remote = g_remoteSnapshot;
    received = g_remoteSnapshotTick;
    haveRemote = g_remoteSnapshotValid;
    velocityValid = g_remoteVelocityValid;
    velocity = g_remoteVelocity;
    ReleaseSRWLockShared(&g_remoteLock);
    if (!haveRemote || GetTickCount()-received > 500 || !ValidNetworkPosition(remote)) {
        WriteProbeLog("BullyCoop v0.16a: WALK SKIPPED: no fresh guest position\r\n");
        return;
    }
    // Target velocity LOOKAHEAD is ONLY for XY. Keep raw incoming Z unchanged.
    // Max 2.4 units; zero if the peer is stationary, too fast, or not yet sampled.
    Position destination = remote;
    float lead = 0.0f;
    if (velocityValid && g_walkLeadCm > 0) {
        const float speed = std::sqrt(velocity.x*velocity.x+velocity.y*velocity.y);
        if (std::isfinite(speed) && speed >= 0.12f && speed <= 5.0f) {
            lead = std::fmin(static_cast<float>(g_walkLeadCm)/100.0f, speed*3.0f);
            destination.x += velocity.x * (lead/speed);
            destination.y += velocity.y * (lead/speed);
        }
    }
    if (!ValidNetworkPosition(destination)) return;
    uint32_t player = 0;
    Position jimmy = {}, npc = {};
    if (!ReadAt(base+kPlayerActorGlobalRva, &player) || !player ||
        ReadJimmyPosition(base,&jimmy) != PositionStatus::Good) {
        WriteProbeLog("BullyCoop v0.16a: WALK SKIPPED: Jimmy unavailable\r\n");
        return;
    }
    typedef void* (__cdecl* LookupPedFn)(int, int);
    LookupPedFn lookup = reinterpret_cast<LookupPedFn>(base + kPedLookupRva);
    void* ped = nullptr;
    __try { ped = lookup(g_npcHandle.load(), 2); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        WriteProbeLog("BullyCoop v0.16a: WALK SKIPPED: NPC lookup exception; AUTO OFF\r\n");
        g_autoWalkEnabled.store(false);
        return;
    }
    const uintptr_t npcAddress = reinterpret_cast<uintptr_t>(ped);
    if (!ped || npcAddress == static_cast<uintptr_t>(player) ||
        !ReadPedWorldPosition(ped,&npc)) {
        WriteProbeLog("BullyCoop v0.16a: WALK SKIPPED: no DISTINCT valid NPC; AUTO OFF\r\n");
        g_autoWalkEnabled.store(false);
        return;
    }
    uint32_t pNested=0,nNested=0,pTransform=0,nTransform=0;
    if (!ReadAt(static_cast<uintptr_t>(player)+0x1554u,&pNested) ||
        !ReadAt(npcAddress+0x1554u,&nNested) ||
        !ReadAt(static_cast<uintptr_t>(pNested?pNested:player)+0x14u,&pTransform) ||
        !ReadAt(static_cast<uintptr_t>(nNested?nNested:npcAddress)+0x14u,&nTransform) ||
        !pTransform || !nTransform || pTransform==nTransform ||
        (pNested && nNested && pNested==nNested)) {
        WriteProbeLog("BullyCoop v0.16a: WALK BLOCKED: actor/transform identity unsafe; AUTO OFF\r\n");
        g_autoWalkEnabled.store(false);
        return;
    }
    if (std::fabs(remote.z-npc.z)>0.45f || std::fabs(jimmy.z-npc.z)>0.45f) {
        WriteProbeLog("BullyCoop v0.16a: WALK SKIPPED: uneven Z, open flat area only\r\n");
        return;
    }
    const BullySafeWalk::XY npcXY={npc.x,npc.y};
    const BullySafeWalk::XY jimmyXY={jimmy.x,jimmy.y};
    const BullySafeWalk::XY rawXY={destination.x,destination.y};
    const BullySafeWalk::Plan plan=BullySafeWalk::compute(npcXY,rawXY,jimmyXY);
    if (plan.status != BullySafeWalk::Status::Move) {
        char line[210]={};
        sprintf_s(line,sizeof(line),
            "BullyCoop v0.16a: WALK %s: dist=%.2f NPC-Jimmy=%.2f raw-Jimmy=%.2f lead=%.2f\r\n",
            plan.status==BullySafeWalk::Status::Reached ? "REACHED" : "UNSAFE",
            plan.remaining, BullySafeWalk::length(BullySafeWalk::sub(npcXY,jimmyXY)),
            BullySafeWalk::length(BullySafeWalk::sub(rawXY,jimmyXY)),lead);
        WriteProbeLog(line);
        return;
    }
    destination.x=plan.waypoint.x;
    destination.y=plan.waypoint.y;
    // IMPORTANT: the actual walk-task destination is the safe waypoint, not
    // the raw or predictive target. Same engine task call as tested v0.13.
    // Same allocation / task-construction / task-attach pipeline as the
    // PedMoveToXYZ script wrapper. Parameters are EXPERIMENTAL inferred values.
    typedef void* (__cdecl* AllocTaskFn)(unsigned int);
    typedef void* (__thiscall* ConstructTaskFn)(void*,void*,const Position*,float,float,bool,bool);
    typedef bool (__thiscall* AttachTaskFn)(void*,void*);
    AllocTaskFn allocate=reinterpret_cast<AllocTaskFn>(base+kWalkTaskAllocatorRva);
    ConstructTaskFn construct=reinterpret_cast<ConstructTaskFn>(base+kWalkTaskConstructorRva);
    AttachTaskFn attach=reinterpret_cast<AttachTaskFn>(base+kWalkTaskAttachRva);
    bool attached=false;
    bool allocated=false;
    __try {
        void* task=allocate(0x34u);
        if (task) {
            allocated=true;
            task=construct(task,ped,&destination,0.70f,0.30f,false,false);
            if (task) attached=attach(reinterpret_cast<void*>(npcAddress+0x5F0u),task);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        WriteProbeLog("BullyCoop v0.16a: WALK TASK Windows exception; AUTO OFF; STOP testing / send log\r\n");
        g_autoWalkEnabled.store(false);
        return;
    }
    char line[480] = {};
    sprintf_s(line,sizeof(line),
        "BullyCoop v0.16a: SAFE WALK TASK handle=%d mode=%s%s raw=%.2f,%.2f waypoint=%.2f,%.2f,%.2f remaining=%.2f step=%.2f gapJimmy=%.2f chordJimmy=%.2f lead=%.2f allocated=%d attached=%d; VERIFY VISUALLY\r\n",
        g_npcHandle.load(),plan.detour?"DETOUR":"DIRECT",plan.catchup?"+CATCHUP":"",remote.x,remote.y,
        destination.x,destination.y,destination.z,plan.remaining,plan.step,plan.gapJimmy,plan.segmentJimmy,lead,allocated?1:0,attached?1:0);
    WriteProbeLog(line);
}

static void RunNpcSpawnExperiment() {
    if (g_spawnAttempted.exchange(true)) return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!VersionMatches(base) || !ValidatePedCreationFunction(base)) {
        WriteProbeLog("BullyCoop v0.7: NPC native signature mismatch; spawn BLOCKED\r\n");
        return;
    }
    Position player = {};
    if (ReadJimmyPosition(base, &player) != PositionStatus::Good) {
        WriteProbeLog("BullyCoop v0.7: Jimmy unavailable; spawn BLOCKED\r\n");
        return;
    }
    // Direct low-level function inferred from the game's PedCreateXYZ script
    // wrapper. Arguments are not fully documented; this is an opt-in EXPERIMENT.
    typedef int (__cdecl* PedCreateFn)(void* scriptContext, int model,
        float x, float y, float z, float rotation, int extra);
    PedCreateFn create = reinterpret_cast<PedCreateFn>(base + kPedCreateLowLevelRva);
    int returnedHandle = -1;
    __try {
        returnedHandle = create(nullptr, static_cast<int>(kModelForProbe),
                                player.x + 3.25f, player.y, player.z + 0.20f, 0.0f, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        WriteProbeLog("BullyCoop v0.7: NPC spawn raised Windows exception; no retry\r\n");
        return;
    }
    char msg[200] = {};
    sprintf_s(msg, sizeof(msg),
        "BullyCoop v0.7: NPC spawn CALL RETURNED handle=%d; verify VISUALLY (not yet proven)\r\n",
        returnedHandle);
    WriteProbeLog(msg);
    if (returnedHandle > 0) {
        g_npcHandle.store(returnedHandle);
        if (g_movementProbeAllowed) {
            WriteProbeLog("BullyCoop v0.16a: NPC spawned; F10 = AUTO WALK ON/OFF (configurable bounded interval); F11 = READ-ONLY ID report\r\n");
        }
    }
}

// WH_GETMESSAGE runs within the game window's OWN thread. It avoids
// overwriting the game window procedure or running game code on our UDP thread.
static LRESULT CALLBACK NpcTestMessageHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == PM_REMOVE && lp) {
        MSG* msg = reinterpret_cast<MSG*>(lp);
        if (g_spawnMessage && msg->message == g_spawnMessage &&
            msg->hwnd == g_gameWindow) {
            if (msg->wParam == 2) g_movePostPending.store(false);
            // F9 spawn and F11 diagnostics require foreground as before.
            // ONLY already-armed automatic walking tasks may run in background,
            // and ONLY for an explicit host bound to 127.0.0.1.
            if (GetForegroundWindow() == g_gameWindow ||
                (msg->wParam == 2 && g_allowBackgroundHostAutoWalk.load())) {
                if (msg->wParam == 0) RunNpcSpawnExperiment();
                else if (msg->wParam == 1) RunNpcIdentityProbe();
                else if (msg->wParam == 2 && g_autoWalkEnabled.load()) RunNpcSingleWalkProbe();
            } else if (msg->wParam == 0) {
                WriteProbeLog("BullyCoop v0.8: NPC spawn blocked (game not foreground)\r\n");
            }
            msg->message = WM_NULL; // Nothing for the game window proc to handle.
            msg->wParam = 0;
            msg->lParam = 0;
        }
    }
    return CallNextHookEx(g_gameMessageHook, code, wp, lp);
}

static BOOL CALLBACK FindBullyTopWindow(HWND wnd, LPARAM user) {
    DWORD pid = 0;
    GetWindowThreadProcessId(wnd, &pid);
    if (pid != GetCurrentProcessId() || GetWindow(wnd, GW_OWNER) || !IsWindowVisible(wnd))
        return TRUE;
    RECT r = {};
    if (!GetClientRect(wnd, &r) || r.right < 640 || r.bottom < 400) return TRUE;
    *reinterpret_cast<HWND*>(user) = wnd;
    return FALSE;
}

static bool InstallNpcTestWindowHook() {
    if (g_gameWindow && g_gameMessageHook) return true;
    HWND wnd = nullptr;
    EnumWindows(FindBullyTopWindow, reinterpret_cast<LPARAM>(&wnd));
    if (!wnd) return false;
    const DWORD windowThread = GetWindowThreadProcessId(wnd, nullptr);
    if (!windowThread) return false;
    const UINT message = RegisterWindowMessageA("BullyCoop_Hamdan_NPC_Loopback_v016");
    if (!message) return false;
    HHOOK hook = SetWindowsHookExA(WH_GETMESSAGE, NpcTestMessageHook, nullptr, windowThread);
    if (!hook) return false;
    g_spawnMessage = message;
    g_gameWindow = wnd;
    g_gameMessageHook = hook;
    WriteProbeLog("BullyCoop v0.16a: NPC test armed; press F9 once INSIDE the game\r\n");
    return true;
}

static DWORD WINAPI PositionThread(LPVOID parameter) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(parameter);
    if (!VersionMatches(base)) return 0;
    WriteProbeLog("\r\n=== BullyCoop Hamdan v0.16a ISOLATED LOOPBACK TWO-WINDOW TEST NEW SESSION ===\r\n");
    WriteProbeLog("BullyCoop position v0.16a: two-window LOCAL LOOPBACK + safe catch-up; no teleport\r\n");
    NetworkProbe network = {};
    const bool networkEnabled = InitNetwork(&network);
    char probeIni[MAX_PATH] = {};
    ReadGameDirectoryFile("BullyCoop.ini", probeIni);
    bool npcExperiment = probeIni[0] &&
        GetPrivateProfileIntA("Experimental", "EnableNPCSpawnProbe", 0, probeIni) == 1;
    if (npcExperiment && ValidatePedCreationFunction(base))
        WriteProbeLog("BullyCoop v0.7: optional NPC creation probe enabled; waiting for game window\r\n");
    else if (npcExperiment) {
        WriteProbeLog("BullyCoop v0.7: NPC native checks FAILED; spawn disabled\r\n");
        npcExperiment = false;
    }
    g_movementProbeAllowed = npcExperiment && networkEnabled && probeIni[0] &&
        GetPrivateProfileIntA("Experimental", "EnableRemoteMovementProbe", 0, probeIni) == 1 &&
        GetPrivateProfileIntA("Experimental", "EnableNPCWalkProbe", 0, probeIni) == 1;
    // v0.15: bounded refresh rate. Avoid per-frame native task allocation.
    // Faster refresh can make movement smoother but may restart current walk tasks;
    // expose a safe range so Hamdan can compare without recompiling.
    if (probeIni[0]) {
        const int requested = GetPrivateProfileIntA("Experimental", "WalkTaskIntervalMs", 1300, probeIni);
        g_walkTaskIntervalMs = requested < 900 ? 900 : (requested > 2800 ? 2800 : static_cast<DWORD>(requested));
    }
    {
        char intervalLine[164] = {};
        sprintf_s(intervalLine, sizeof(intervalLine),
            "BullyCoop v0.16a: configured WalkTaskIntervalMs=%lu (bounded 900-2800, default 1300)\r\n",
            static_cast<unsigned long>(g_walkTaskIntervalMs));
        WriteProbeLog(intervalLine);
    }
    if (probeIni[0]) {
        const int requestedLeadCm = GetPrivateProfileIntA("Experimental", "WalkLeadCm", 180, probeIni);
        g_walkLeadCm = requestedLeadCm < 0 ? 0 : (requestedLeadCm > 240 ? 240 : static_cast<DWORD>(requestedLeadCm));
    }
    {
        char leadLine[150] = {};
        sprintf_s(leadLine, sizeof(leadLine),
            "BullyCoop v0.16a: configured WalkLeadCm=%lu (bounded 0-240; 0 disables prediction)\r\n",
            static_cast<unsigned long>(g_walkLeadCm));
        WriteProbeLog(leadLine);
    }
    if (g_movementProbeAllowed) {
        g_movementNativeVerified = ValidatePedMovementFunction(base) && ValidateWalkTaskFunction(base);
        WriteProbeLog(g_movementNativeVerified ?
            "BullyCoop v0.16a: WALK native signatures verified OFFLINE; F10 toggles AUTO WALK tasks (risky)\r\n" :
            "BullyCoop v0.16a: WALK signature mismatch; experimental movement disabled\r\n");
        if (!g_movementNativeVerified) g_movementProbeAllowed = false;
    } else {
        WriteProbeLog("BullyCoop v0.16a: AUTO walking DISABLED (safe default); F9 spawn optional\r\n");
    }
    bool previouslyAvailable = false;
    Position previous = {};
    DWORD lastLog = 0;
    DWORD lastWait = 0;
    PositionStatus previousStatus = PositionStatus::Good;
    bool f9PreviouslyDown = false;
    bool f10PreviouslyDown = false;
    bool f11PreviouslyDown = false;
    DWORD lastWalkPress = 0;
    DWORD lastAutoDispatch = 0; // worker-thread only; bounded configurable MIN spacing
    DWORD lastWindowTry = 0;
    while (true) {
        Sleep(100); // 10 reads/s for network; local log throttled below.
        Position now = {};
        const DWORD tick = GetTickCount();
        const PositionStatus status = ReadJimmyPosition(base, &now);
        if (status == PositionStatus::Good) {
            const bool moved = !previouslyAvailable ||
                (std::fabs(now.x - previous.x) + std::fabs(now.y - previous.y) +
                 std::fabs(now.z - previous.z)) > 0.2f;
            if (moved || tick - lastLog >= 10000) {
                char line[160] = {};
                sprintf_s(line, sizeof(line), "BullyCoop position: x=%.2f y=%.2f z=%.2f time_ms=%lu\r\n",
                          now.x, now.y, now.z, static_cast<unsigned long>(tick));
                WriteProbeLog(line);
                previous = now;
                lastLog = tick;
            }
            previouslyAvailable = true;
        } else {
            if (previouslyAvailable || status != previousStatus || tick - lastWait >= 15000) {
                char line[180] = {};
                sprintf_s(line, sizeof(line), "BullyCoop position: waiting: %s\r\n", StatusName(status));
                WriteProbeLog(line);
                lastWait = tick;
            }
            previouslyAvailable = false;
        }
        previousStatus = status;
        if (npcExperiment && !g_spawnAttempted.load() &&
            tick - lastWindowTry > 3000 && !g_gameWindow) {
            lastWindowTry = tick;
            InstallNpcTestWindowHook();
        }
        const bool f9Down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (npcExperiment && g_gameWindow && !g_spawnAttempted.load() &&
            f9Down && !f9PreviouslyDown && status == PositionStatus::Good &&
            GetForegroundWindow() == g_gameWindow) {
            WriteProbeLog("BullyCoop v0.7: F9 pressed; dispatching NPC test on game thread\r\n");
            if (!PostMessageA(g_gameWindow, g_spawnMessage, 0, 0))
                WriteProbeLog("BullyCoop v0.7: PostMessage failed; no spawn\r\n");
        }
        f9PreviouslyDown = f9Down;
        const bool f10Down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
        if (g_movementProbeAllowed && f10Down && !f10PreviouslyDown &&
            status == PositionStatus::Good && g_npcHandle.load() > 0 &&
            g_gameWindow && GetForegroundWindow() == g_gameWindow &&
            (!lastWalkPress || tick-lastWalkPress >= 800)) {
            lastWalkPress=tick;
            const bool newState = !g_autoWalkEnabled.load();
            g_autoWalkEnabled.store(newState);
            if (newState) {
                // Prompt first task soon, but still wait for an actual guest update.
                lastAutoDispatch = tick - (g_walkTaskIntervalMs - 100);
                char line[150]={};
                sprintf_s(line,sizeof(line),
                    "BullyCoop v0.16a: F10 -> AUTO WALK ENABLED (tasks >=%lu ms apart)\r\n",
                    static_cast<unsigned long>(g_walkTaskIntervalMs));
                WriteProbeLog(line);
            } else {
                WriteProbeLog("BullyCoop v0.16a: F10 -> AUTO WALK DISABLED (last engine task may finish)\r\n");
            }
        }
        f10PreviouslyDown = f10Down;
        const bool f11Down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        if (g_movementProbeAllowed && f11Down && !f11PreviouslyDown &&
            status == PositionStatus::Good && g_npcHandle.load() > 0 &&
            GetForegroundWindow() == g_gameWindow) {
            g_followEnabled.store(!g_followEnabled.load());
            WriteProbeLog("BullyCoop v0.16a: F11 -> READ-ONLY actor identity diagnostics toggled\r\n");
        }
        f11PreviouslyDown=f11Down;
        if (networkEnabled) {
            // Guest initiates contact by sending its local coordinates.
            // Host replies only after a valid guest packet has been received.
            if (status == PositionStatus::Good) SendNetworkPosition(&network, now);
            ReceiveNetworkPositions(&network, tick);
            PublishRemoteSnapshot(network);
            // F10 is an opt-in auto dispatcher, NOT a per-frame teleport.
            // All engine calls remain on the game window thread, spaced >= g_walkTaskIntervalMs.
            // No valid / fresh network packet means NO new movement tasks.
            if (g_movementProbeAllowed && g_autoWalkEnabled.load() &&
                g_npcHandle.load()>0 && status==PositionStatus::Good &&
                g_gameWindow &&
                (GetForegroundWindow()==g_gameWindow || g_allowBackgroundHostAutoWalk.load()) &&
                network.haveRemote && tick-network.lastReceived <= 450 &&
                tick-lastAutoDispatch >= g_walkTaskIntervalMs &&
                !g_movePostPending.exchange(true)) {
                lastAutoDispatch=tick;
                if (!PostMessageA(g_gameWindow,g_spawnMessage,2,0)) {
                    g_movePostPending.store(false);
                    g_autoWalkEnabled.store(false);
                    WriteProbeLog("BullyCoop v0.16a: AUTO WALK PostMessage failed; AUTO OFF\r\n");
                }
            }
            // F11 requests read-only diagnostic callbacks only.
            if (g_movementProbeAllowed && g_followEnabled.load() &&
                g_npcHandle.load()>0 && status==PositionStatus::Good &&
                g_gameWindow && GetForegroundWindow()==g_gameWindow &&
                tick - g_lastFollowLog >= 1000)
                PostMessageA(g_gameWindow,g_spawnMessage,1,0);
        }
    }
    // Unreachable during normal game lifetime.
    return 0;
}

static BOOL CALLBACK StartProbe(PINIT_ONCE, PVOID, PVOID*) {
    HMODULE module = GetModuleHandleA(nullptr);
    if (!module) return TRUE;
    // Pin this DLL: the background thread should never run code from an unloaded module.
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCSTR>(&StartProbe), &self)) return TRUE;
    HANDLE thread = CreateThread(nullptr, 0, PositionThread, module, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        WriteProbeLog("BullyCoop position: unable to start reader thread\r\n");
        FreeLibrary(self);
    }
    return TRUE;
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version,
                                               REFIID iid, LPVOID* out, LPUNKNOWN outer) {
    typedef HRESULT (WINAPI* NativeFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    FARPROC proc = GetNativeProc("DirectInput8Create");
    if (!proc) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    const HRESULT result = reinterpret_cast<NativeFn>(proc)(instance, version, iid, out, outer);
    if (SUCCEEDED(result)) {
        WriteProbeLog("BullyCoop probe: DirectInput8Create forwarded successfully\r\n");
        InitOnceExecuteOnce(&g_probe_init, StartProbe, nullptr, nullptr);
    }
    return result;
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    typedef HRESULT (WINAPI* NativeFn)();
    FARPROC fn = GetNativeProc("DllCanUnloadNow");
    return fn ? reinterpret_cast<NativeFn>(fn)() : S_FALSE;
}
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* out) {
    typedef HRESULT (WINAPI* NativeFn)(REFCLSID, REFIID, LPVOID*);
    FARPROC fn = GetNativeProc("DllGetClassObject");
    return fn ? reinterpret_cast<NativeFn>(fn)(clsid, iid, out) : CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    typedef HRESULT (WINAPI* NativeFn)();
    FARPROC fn = GetNativeProc("DllRegisterServer");
    return fn ? reinterpret_cast<NativeFn>(fn)() : E_NOTIMPL;
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    typedef HRESULT (WINAPI* NativeFn)();
    FARPROC fn = GetNativeProc("DllUnregisterServer");
    return fn ? reinterpret_cast<NativeFn>(fn)() : E_NOTIMPL;
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
