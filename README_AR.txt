Bully Co-op Hamdan v0.9 - FLAT-GROUND SAFE MOVE TEST (SOURCE ONLY)
================================================================
IMPORTANT: This is not a finished co-op mod. v0.8 glitched through ground/walls on Hamdan's computer; its movement MUST NOT be used. v0.9 has NOT been compiled or game-tested.

WHAT ACTUALLY HAPPENED IN v0.8:
- The F9 spawn works visually (confirmed by user).
- F10 force-teleported the character to received X/Y/Z every ~150ms.
- The log recorded Z jumps 5.81 -> -1.43 -> 8.71 -> 11.41, and a vanished NPC handle in one session.
- Fake guest was drawing a circle around Jimmy without validating ground, walls or different areas.
- Toggling F10 OFF returns NPC to engine-native behavior; game physics likely fights forced positioning.

WHAT CHANGED IN v0.9:
- Never directly copy remote Z to ped; retain its OWN current Z.
- Read NPC world position and move only <=0.12 units PER invocation toward valid horizontal targets.
- Reject guest X/Y more than 6 units from Jimmy or more than 5 units from NPC; NPC >10 from Jimmy also blocked.
- Reject if player or packet height differs >0.65 from spawn height, or NPC Z differs >0.90; STOP if unsafe.
- Require fresh guest data <=450ms; only one queued movement message at a time.
- If NPC handle no longer exists, disable movement and require GAME RESTART.
- Fake guest orbit reduced from 3.0 to 1.8 units, speed 0.65 to 0.30 rad/s.

LIMITATION: This still directly steps NPC position; it is NOT pathfinding or collision-safe. If there is a wall in the path the NPC can STILL clip through. TEST ONLY on flat open ground away from walls/steps/pools/roofs. A real fix needs a verified engine AI pathfinding/walking primitive. The first crash's exact cause is NOT proven by the log.

BUILD IN GITHUB:
1. BACK UP your old dinput8.dll and game folder/save files.
2. Upload ZIP contents to GitHub repo; make sure bridge/dinput8_proxy.cpp and BUILD_WINDOWS.cmd are replaced.
3. GitHub Actions -> Build Bully Co-op SAFE NPC Movement v0.9 (x86).
4. Extract artifact out/dinput8.dll and replace DLL in a SEPARATE TEST COPY of Bully.
5. Place BullyCoop.ini next to Bully.exe:
[Network]
Enabled=1
Role=host
Port=7791
SessionCode=246813
[Experimental]
EnableNPCSpawnProbe=1
EnableRemoteMovementProbe=1

ONE-PC TEST:
6. Run FAKE_GUEST_NEAR_JIMMY.cmd, keep open.
7. Start Bully once, load free roam, find OPEN FLAT outdoor area.
8. Press F9 ONCE to spawn; F10 ONCE to attempt SMALL XY steps; F10 again to stop.
9. If anything glitches, F10 OFF, quit WITHOUT saving, and send fresh BullyCoop_bridge.log.
10. Check log for v0.9 NPC STEP or movement SKIPPED/unsafe/ AUTO-DISABLED.

EMERGENCY DISABLE: change EnableRemoteMovementProbe=0 and restart; F9 spawn still works.
NO GUARANTEES: not compiled on Windows or tested inside game by the author.
