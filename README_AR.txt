Bully Co-op Hamdan v0.15c — SAFE CATCH-UP / STAND-OFF (SOURCE ONLY)

WHY: last v0.15b log had multiple WALK SKIPPED attempts at dist 9..23 XY units: the old 8-unit cap blocked catch-up indefinitely. Other skips came from the straight walk chord crossing within ~2.75 of Jimmy. User reports occasional smooth walking but recurring stalls.

CHANGES
- Distances greater than 8 XY units are broken into at most 5.8-unit engine walking tasks, rather than rejected.
- If walking in a straight line would pass near Jimmy, a side-step / orbit-style waypoint is planned OUTSIDE his protective radius.
- If predictive remote target ends up near Jimmy, the waypoint remains at safe stand-off distance.
- Strict NPC vs player ACTOR and TRANSFORM identity checks retained.
- Strict flat Z ground check retained; no teleport, no direct position writing, no per-frame calls.
- The source now has standalone C++ planner tests, compiled by GitHub Actions' Windows build script.
- Logs explicitly name DIRECT or DETOUR / CATCHUP / REACHED / UNSAFE.

HOW TO BUILD
1. Upload the extracted ZIP content to GitHub project gntaz7-pixel/VCCoop-Hamdan5.
2. GitHub Actions -> Build Bully Co-op Safe Catchup v0.15c (x86).
3. Get out/dinput8.dll from Actions artifact BullyCoop-Hamdan-SafeCatchup-v15c-x86.
4. Preserve last working v0.15b DLL for rollback and use a separate test copy of Bully.
5. Place BullyCoop.ini in Bully.exe directory, using the host INI below.
6. In a wide FLAT area, start only this ZIP's FAKE_GUEST_NEAR_JIMMY.cmd and wait HOST FOUND!
7. F9 spawn once, F10 ON, watch 20 seconds; F10 OFF (last walking task may finish).
8. Send BullyCoop_bridge.log and describe NPC stop-go and whether base Jimmy works.

BullyCoop.ini
[Network]
Enabled=1
Role=host
Port=7791
SessionCode=246813

[Experimental]
EnableNPCSpawnProbe=1
EnableRemoteMovementProbe=1
EnableNPCWalkProbe=1
WalkTaskIntervalMs=1300
WalkLeadCm=180

LIMITATIONS
- Only test geometry / sources verified offline; no real Windows compile or Bully game run performed here.
- Geometry checks do NOT navigate walls, slopes, NPC collisions, or obstacles. Open flat area ONLY.
- Do not save; stop immediately if unintended Jimmy movement, terrain clipping, or crash.
- This is a fake guest, NOT 2-device online multiplayer. Next milestone (v0.16) = first 2-device LAN positions and walking.
- If NPC is despawned and logs "no DISTINCT valid NPC; AUTO OFF", reload test session rather than forcing stale handle.
