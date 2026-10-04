Bully Co-op Hamdan v0.11 - NPC Anti-Stall Recovery Experiment (SOURCE ONLY)
==================================================================
WHY v0.10 FROZE:
- User confirmed: F9 NPC did appear, F10 only toggled movement in the log.
- v0.10 logged repeated "guest too close to Jimmy" and refused ALL motion.
- The scripted fake guest can approach Jimmy if Jimmy walks toward it.
- A stationary test NPC can physically obstruct Jimmy.

CHANGES:
1. Small recovery step AWAY from Jimmy if the NPC is already within 2.65 game units.
2. When guest target is too close, project the target outward to a 3.20 unit safe radius, rather than freezing.
3. Tiny sideways step when the direct step would enter the 2.35 unit keep-out circle.
4. Fake guest test orbit is 4.0 units from the last reported Jimmy coordinate.
5. Test spawn is at Jimmy.x+3.25. F9 still spawns ONCE per game session; F10 only toggles movement.
6. v0.9 safeguards kept: read NPC's own Z, no copying network Z, max 0.12 XY step/135ms, stale packet refusal, script calls on game window thread. Engine pathfinding and wall collision still NOT implemented.

HOW TO BUILD:
1. Backup the known-good v0.9 DLL and your Bully test folder; DO NOT change main install.
2. Upload all ZIP files to the root of the GitHub project, preserving folders.
3. GitHub Actions -> Build Bully Co-op Anti-Stall NPC Movement v0.11 (x86).
4. Download artifact out/dinput8.dll, put next to Bully.exe in a separate TEST copy.
5. BullyCoop.ini:
[Network]
Enabled=1
Role=host
Port=7791
SessionCode=246813

[Experimental]
EnableNPCSpawnProbe=1
EnableRemoteMovementProbe=1

6. CLOSE all old fake-guest CMD windows (Ctrl+C). ONLY ONE can be connected at once.
7. START Bully.exe as HOST FIRST; enter gameplay on flat open ground.
8. Launch FAKE_GUEST_NEAR_JIMMY.cmd from THIS ZIP; wait for green HOST FOUND.
9. PRESS F9 ONCE, TURN CAMERA to see NPC beside Jimmy (3.25 units along X), then F10 ONCE.
10. Report actual NPC behavior and upload fresh bridge log. F10 again disables experimental movement.

NOTE: This is a direct-coordinate MOVE PROBE, NOT walk animations, NOT pathfinding, NOT verified co-op.
If Jimmy/NPC sticks, do NOT save game, turn F10 OFF, then quit; restore v0.9 DLL if needed.
