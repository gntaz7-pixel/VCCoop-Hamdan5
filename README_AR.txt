Bully Co-op — Hamdan Edition v0.14 (SOURCE ONLY)

CURRENT RESULTS VERIFIED BY HAMDAN: v0.13 spawned NPC and NPC visibly walked a little on each F10 press. Log showed engine walk tasks attached=1, and Jimmy's logged location was stable while walk tasks were issued. This is NOT fully working multiplayer.

NEW v0.14 EXPERIMENT — AUTO ENGINE WALK TASKS:
F9: Spawn one NPC in free roam; only once per game session.
F10: Toggle AUTO walk task issuing ON/OFF. When enabled, and only if GUEST has fresh coordinate packets, a walk task is sent every 2.8 seconds at most. NO PedSetPosXYZ. F10 OFF stops NEW tasks; already issued engine walking task can finish.
F11: Toggle read-only identity diagnostics.

SAFETY: Test on COPIED game folder ONLY; back up original DLL, never save after test. Risk of crash, task allocation leak, NPC despawn, wall collision or Jimmy movement. If Jimmy is moved involuntarily, STOP experiment and restore v0.13/v0.12 immediately.

BullyCoop.ini (HOST):
[Network]
Enabled=1
Role=host
Port=7791
SessionCode=246813
[Experimental]
EnableNPCSpawnProbe=1
EnableRemoteMovementProbe=1
EnableNPCWalkProbe=1

HOW TO TEST:
1. GitHub upload all files, let GitHub Actions build Windows x86 DLL. This ZIP contains SOURCE ONLY.
2. Keep backup of working dinput8.dll and install new compiled DLL beside Bully.exe in copied game folder.
3. Close other FAKE_GUEST processes/windows. Start Bully.exe as host, reach OPEN FLAT ground away from missions/walls.
4. Launch included FAKE_GUEST_NEAR_JIMMY.cmd and verify 'FAKE GUEST v0.14' / 'HOST FOUND'. This is only a simulated player, not a real multiplayer client.
5. Press F9 once to spawn the second NPC. Press F10 ONCE to enable repeated engine walk tasks. Watch for ~15 seconds while moving Jimmy a little.
6. Press F10 again to disable NEW tasks. Previous walk may finish. If anything goes wrong, close game WITHOUT saving and send BullyCoop_bridge.log.

WHY THIS IS SAFER THAN v0.9-v0.11: no forced location changes. Movement tasks use the engine routines observed to work in v0.13. Target is refused if distance exceeds 8.0, if too close to Jimmy, if the straight-line segment to target would pass within 2.75 of Jimmy, or if elevation difference is large. Only sends tasks on game's own window thread, at most once per 2.8 seconds with fresh network packet. It is STILL EXPERIMENTAL and cannot guarantee collision/path correctness or character animations.

SUPPORTED GAME: same Bully.exe 8,204,288 bytes as prior tests. Do not use in other versions. Network protocol is UNENCRYPTED: private trusted LAN only. This is not complete co-op. No mission sync, fights, cutscenes, saves, net security or reliable remote player control.
