Bully Co-op - Hamdan Edition v0.13 (SOURCE ONLY)

NEW: ONE-SHOT PED WALK TASK - EXPERIMENTAL, UNTESTED IN GAME.
Bully.exe exactly 8,204,288 bytes. Confirmed F9 spawn and v0.12 separate NPC actor pointers in previous test.

F9 - Create ONE NPC (once per game session).
F10 - Submit ONE walking task to the NPC based on latest guest position. No teleport, no continuous forced movement. Each press is a single experiment, not on/off. May CRASH, FREEZE or affect Jimmy (unverified!).
F11 - Toggle READ-ONLY actor pointer report (like v0.12).

SAFETY: In BullyCoop.ini set EnableNPCWalkProbe=0 first, check Bully runs and F9 spawns. Only after backup, open flat ground and change EnableNPCWalkProbe=1. Do not save.

Network enabled: HOST local UDP 7791 SessionCode 246813.
[Experimental]
EnableNPCSpawnProbe=1
EnableRemoteMovementProbe=1
EnableNPCWalkProbe=1

Testing: Close all OLD FAKE_GUEST tools. Start Bully HOST and wait until in free-roam outdoor FLAT open ground. Start FAKE_GUEST_NEAR_JIMMY.cmd from v0.11 ZIP (SAFE 4.0m orbit), wait HOST FOUND. F9 once, ensure NPC starts separated. Press F10 ONCE, observe NPC and Jimmy. F10 again only if first walk task was safely completed and NPC is still away from Jimmy; there is NO stop button for engine task once issued. F11 toggle read-only logging. Stop immediately if Jimmy moves involuntarily. Send BullyCoop_bridge.log.

Static inspection found PedMoveToXYZ script wrapper -> task allocator(0x5EEAA0), task constructor(0x4705B0), task attachment(0x471390), matching this Bully.exe. Task calling convention and suitability in injected DLL remain UNPROVEN. Runtime code verifies byte signatures and separate NPC transform.

TEST ON A BACKUP ONLY. Not yet full co-op. Do not share on public untrusted network. Builds through GitHub Actions, Windows x86.
