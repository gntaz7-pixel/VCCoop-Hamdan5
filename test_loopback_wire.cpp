#include "loopback_wire.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace BullyLoopback;
    static_assert(kNetworkVersion == 2, "Must reject original fake guest protocol");
    const uint32_t code = 593741;
    assert(ValidEnvelope(kNetworkMagic,2,2,code,code,true)); // Host accepts new guest
    assert(ValidEnvelope(kNetworkMagic,2,1,code,code,false)); // Guest accepts new host
    assert(!ValidEnvelope(kNetworkMagic,1,2,code,code,true)); // Old fake guest rejected
    assert(!ValidEnvelope(kNetworkMagic,2,2,246813,code,true)); // Wrong code rejected
    assert(!ValidEnvelope(kNetworkMagic,2,1,code,code,true)); // Wrong role rejected
    assert(!ValidEnvelope(0,2,2,code,code,true)); // Wrong magic rejected
    puts("PASS: loopback v2 rejects fake v1/wrong code/role/magic");
    return 0;
}
