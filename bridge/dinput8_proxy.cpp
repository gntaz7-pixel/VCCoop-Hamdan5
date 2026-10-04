// Bully Co-op Hamdan | SAFE NPC movement experiment v0.9 | Bully: Scholarship Edition (user-provided build only).
// EXPERIMENT v0.9: flat-ground limited XY steps only; Z is NOT copied from network to the NPC.
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
#pragma comment(lib, "Ws2_32.lib")

static INIT_ONCE g_input_init = INIT_ONCE_STATIC_INIT;
static INIT_ONCE g_probe_init = INIT_ONCE_STATIC_INIT;
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
static const uint32_t kNetworkMagic = 0x42434f50u; // "BCOP"
static const uint16_t kNetworkVersion = 1;
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
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
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
            if (WSAGetLastError() != WSAEWOULDBLOCK)
                WriteProbeLog("BullyCoop net: UDP receive error\r\n");
            break;
        }
        if (count != sizeof(packet) || sender.sin_family != AF_INET ||
            ntohl(packet.magic) != kNetworkMagic || ntohs(packet.version) != kNetworkVersion ||
            ntohl(packet.sessionCode) != net->sessionCode ||
            ntohs(packet.senderRole) != (net->host ? 2 : 1)) continue;
        Position pos = {packet.x, packet.y, packet.z};
        if (!ValidNetworkPosition(pos)) continue;
        if (net->peerKnown) {
            if (sender.sin_addr.s_addr != net->peer.sin_addr.s_addr ||
                sender.sin_port != net->peer.sin_port) continue;
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
            WriteProbeLog("BullyCoop net: first remote player position RECEIVED\r\n");
            net->previouslyConnected = true;
        }
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
static std::atomic<bool> g_followEnabled{false};
static bool g_movementProbeAllowed = false;
static bool g_movementNativeVerified = false;
static DWORD g_lastFollowLog = 0;
static DWORD g_lastMoveSkipLog = 0;
// Once we create a NPC, keep a fixed flat-ground safety anchor for this test session.
// This is not pathfinding, ground detection, or reliable live remote-player movement.
static Position g_npcSpawnAnchor = {};
static bool g_npcSpawnAnchorValid = false;
static DWORD g_lastNpcMoveTick = 0;
static std::atomic<bool> g_movePostPending{false};
static const float kMaxTargetFromJimmy = 6.0f; // avoid off-screen / far-away teleports
static const float kMaxNpcFromJimmy = 10.0f;
static const float kMaxRemoteDzFromSpawn = 0.65f; // FLAT GROUND only
static const float kMaxNpcDzFromSpawn = 0.90f;
static const float kMaxHorizontalStepPerCall = 0.12f; // <=0.8m/s at 150ms
static const float kMaxTargetFromNpc = 5.0f; // must not chase/teleport across distant geometry
// Only the UDP thread writes this mailbox. Window thread reads a snapshot.
static SRWLOCK g_remoteLock = SRWLOCK_INIT;
static Position g_remoteSnapshot = {};
static DWORD g_remoteSnapshotTick = 0;
static bool g_remoteSnapshotValid = false;

static void PublishRemoteSnapshot(const NetworkProbe& network) {
    AcquireSRWLockExclusive(&g_remoteLock);
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

static void RunNpcMovementExperiment() {
    if (!g_followEnabled.load() || !g_movementProbeAllowed || !g_movementNativeVerified ||
        !g_npcSpawnAnchorValid) return;
    const int handle = g_npcHandle.load();
    if (handle <= 0) return;
    const DWORD tick = GetTickCount();
    // Ignore queued movement messages if the game becomes busy or loses focus.
    if (g_lastNpcMoveTick && tick - g_lastNpcMoveTick < 135) return;
    Position remote = {};
    DWORD lastReceived = 0;
    bool haveRemote = false;
    AcquireSRWLockShared(&g_remoteLock);
    remote = g_remoteSnapshot;
    lastReceived = g_remoteSnapshotTick;
    haveRemote = g_remoteSnapshotValid;
    ReleaseSRWLockShared(&g_remoteLock);
    if (!haveRemote || tick - lastReceived > 450 || !ValidNetworkPosition(remote)) {
        if (tick - g_lastMoveSkipLog >= 4000) {
            WriteProbeLog("BullyCoop v0.9: STOPPED updating: stale/invalid remote position\r\n");
            g_lastMoveSkipLog = tick;
        }
        return;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    Position jimmy = {};
    if (ReadJimmyPosition(base, &jimmy) != PositionStatus::Good) return;
    if (std::fabs(remote.z - g_npcSpawnAnchor.z) > kMaxRemoteDzFromSpawn ||
        std::fabs(jimmy.z - g_npcSpawnAnchor.z) > kMaxRemoteDzFromSpawn) {
        // Never copy wild under-ground/stair/roof Z levels into NPC's location.
        if (tick - g_lastMoveSkipLog >= 4000) {
            WriteProbeLog("BullyCoop v0.9: move SKIPPED: Z changed; FLAT GROUND ONLY\r\n");
            g_lastMoveSkipLog = tick;
        }
        return;
    }
    const float rx = remote.x - jimmy.x;
    const float ry = remote.y - jimmy.y;
    if (rx*rx + ry*ry > kMaxTargetFromJimmy*kMaxTargetFromJimmy) {
        if (tick - g_lastMoveSkipLog >= 4000) {
            WriteProbeLog("BullyCoop v0.9: move SKIPPED: guest >6m from Jimmy\r\n");
            g_lastMoveSkipLog = tick;
        }
        return;
    }
    typedef void* (__cdecl* LookupPedFn)(int handle, int actorType);
    typedef void (__cdecl* SetActorPositionFn)(void* actor, const Position* xyz);
    LookupPedFn lookup = reinterpret_cast<LookupPedFn>(base + kPedLookupRva);
    SetActorPositionFn setPosition = reinterpret_cast<SetActorPositionFn>(base + kPedSetPosLowLevelRva);
    bool missingPed = false, applied = false, outOfRange = false, tooFar = false;
    Position stepped = {};
    __try {
        void* npc = lookup(handle, 2); // NPC/ped, NEVER player actorType=3
        if (!npc) {
            missingPed = true;
        } else {
            Position current = {};
            if (!ReadPedWorldPosition(npc, &current) ||
                std::fabs(current.z - g_npcSpawnAnchor.z) > kMaxNpcDzFromSpawn) {
                outOfRange = true;
            } else {
                const float npcFromJimmyX = current.x - jimmy.x;
                const float npcFromJimmyY = current.y - jimmy.y;
                const float dX = remote.x - current.x;
                const float dY = remote.y - current.y;
                const float d2 = dX*dX + dY*dY;
                if (npcFromJimmyX*npcFromJimmyX + npcFromJimmyY*npcFromJimmyY >
                     kMaxNpcFromJimmy*kMaxNpcFromJimmy ||
                    d2 > kMaxTargetFromNpc*kMaxTargetFromNpc) {
                    tooFar = true;
                } else if (d2 > 0.025f * 0.025f) {
                    const float d = std::sqrt(d2);
                    const float frac = (d > kMaxHorizontalStepPerCall) ?
                        kMaxHorizontalStepPerCall / d : 1.0f;
                    stepped.x = current.x + dX * frac;
                    stepped.y = current.y + dY * frac;
                    stepped.z = current.z; // CRITICAL: retain actor's own Z; never remote Z
                    setPosition(npc, &stepped);
                    applied = true;
                    g_lastNpcMoveTick = tick;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_followEnabled.store(false);
        WriteProbeLog("BullyCoop v0.9: NPC position call raised Windows exception; AUTO-DISABLED\r\n");
        return;
    }
    if (missingPed || outOfRange) {
        g_followEnabled.store(false);
        if (missingPed) g_npcHandle.store(-1);
        WriteProbeLog(missingPed ?
            "BullyCoop v0.9: NPC handle disappeared; AUTO-DISABLED; restart to respawn\r\n" :
            "BullyCoop v0.9: NPC position/Z unsafe; AUTO-DISABLED\r\n");
    } else if (tooFar) {
        if (tick - g_lastMoveSkipLog >= 4000) {
            WriteProbeLog("BullyCoop v0.9: movement SKIPPED: NPC too far from target; no teleport\r\n");
            g_lastMoveSkipLog = tick;
        }
    } else if (applied && (g_lastFollowLog == 0 || tick - g_lastFollowLog >= 2500)) {
        char msg[200] = {};
        sprintf_s(msg, sizeof(msg),
            "BullyCoop v0.9: NPC STEP handle=%d x=%.2f y=%.2f z=%.2f age_ms=%lu\r\n",
            handle, stepped.x, stepped.y, stepped.z, static_cast<unsigned long>(tick-lastReceived));
        WriteProbeLog(msg);
        g_lastFollowLog = tick;
    }
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
                                player.x + 2.5f, player.y, player.z + 0.20f, 0.0f, 1);
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
        g_npcSpawnAnchor = player;
        g_npcSpawnAnchorValid = true;
        g_lastNpcMoveTick = 0;
        if (g_movementProbeAllowed) {
            WriteProbeLog("BullyCoop v0.9: NPC spawned; press F10 to toggle SAFE XY steps\r\n");
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
            if (msg->wParam == 1) g_movePostPending.store(false);
            if (GetForegroundWindow() == g_gameWindow) {
                if (msg->wParam == 0) RunNpcSpawnExperiment();
                else if (msg->wParam == 1) RunNpcMovementExperiment();
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
    const UINT message = RegisterWindowMessageA("BullyCoop_Hamdan_NPC_SafeMove_v09");
    if (!message) return false;
    HHOOK hook = SetWindowsHookExA(WH_GETMESSAGE, NpcTestMessageHook, nullptr, windowThread);
    if (!hook) return false;
    g_spawnMessage = message;
    g_gameWindow = wnd;
    g_gameMessageHook = hook;
    WriteProbeLog("BullyCoop v0.9: NPC test armed; press F9 once INSIDE the game\r\n");
    return true;
}

static DWORD WINAPI PositionThread(LPVOID parameter) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(parameter);
    if (!VersionMatches(base)) return 0;
    WriteProbeLog("\r\n=== BullyCoop Hamdan v0.9 NEW SESSION ===\r\n");
    WriteProbeLog("BullyCoop position v0.9: network + opt-in NPC spawn/SAFE movement probe\r\n");
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
        GetPrivateProfileIntA("Experimental", "EnableRemoteMovementProbe", 0, probeIni) == 1;
    if (g_movementProbeAllowed) {
        g_movementNativeVerified = ValidatePedMovementFunction(base);
        WriteProbeLog(g_movementNativeVerified ?
            "BullyCoop v0.9: SAFE movement signatures VERIFIED; press F9 then F10 (flat area only)\r\n" :
            "BullyCoop v0.9: movement native signature mismatch; MOVEMENT BLOCKED\r\n");
        if (!g_movementNativeVerified) g_movementProbeAllowed = false;
    } else {
        WriteProbeLog("BullyCoop v0.9: movement DISABLED (safe default)\r\n");
    }
    bool previouslyAvailable = false;
    Position previous = {};
    DWORD lastLog = 0;
    DWORD lastWait = 0;
    PositionStatus previousStatus = PositionStatus::Good;
    bool f9PreviouslyDown = false;
    bool f10PreviouslyDown = false;
    DWORD lastMovePost = 0;
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
            GetForegroundWindow() == g_gameWindow) {
            const bool turnOn = !g_followEnabled.load();
            g_followEnabled.store(turnOn);
            if (turnOn) g_lastNpcMoveTick = 0;
            WriteProbeLog(turnOn ?
                "BullyCoop v0.9: F10 -> SAFE NPC movement ENABLED\r\n" :
                "BullyCoop v0.9: F10 -> SAFE NPC movement STOPPED\r\n");
        }
        f10PreviouslyDown = f10Down;
        if (networkEnabled) {
            // Guest initiates contact by sending its local coordinates.
            // Host replies only after a valid guest packet has been received.
            if (status == PositionStatus::Good) SendNetworkPosition(&network, now);
            ReceiveNetworkPositions(&network, tick);
            PublishRemoteSnapshot(network);
            if (g_movementProbeAllowed && g_followEnabled.load() &&
                g_npcHandle.load() > 0 && status == PositionStatus::Good &&
                g_gameWindow && GetForegroundWindow() == g_gameWindow &&
                tick - lastMovePost >= 150 && !g_movePostPending.load()) {
                // All Bully engine calls stay on the game window thread.
                g_movePostPending.store(true);
                if (PostMessageA(g_gameWindow, g_spawnMessage, 1, 0))
                    lastMovePost = tick;
                else
                    g_movePostPending.store(false);
            }
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
