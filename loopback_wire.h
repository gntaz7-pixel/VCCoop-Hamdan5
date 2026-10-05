#pragma once
// Wire envelope validation is pure C++ and tested without loading Bully.exe.
// v2 rejects v1 packets sent by earlier FAKE_GUEST scripts and older DLLs.
// Both HOST and GUEST must run the SAME v0.16a DLL.
#include <cstdint>
namespace BullyLoopback {
static constexpr uint32_t kNetworkMagic = 0x42434f50u; // BCOP
static constexpr uint16_t kNetworkVersion = 2;
static inline bool ValidEnvelope(uint32_t magic, uint16_t version,
    uint16_t senderRole, uint32_t sessionCode, uint32_t configuredCode,
    bool receiverIsHost) {
    return magic == kNetworkMagic && version == kNetworkVersion &&
           senderRole == (receiverIsHost ? 2 : 1) &&
           sessionCode == configuredCode;
}
}
