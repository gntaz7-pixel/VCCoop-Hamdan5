Bully Co-op Hamdan v0.10 - Anti-Stick / Separation Test (SOURCE ONLY)
================================================================
What Hamdan confirmed: v0.7 NPC appears. v0.9 NPC motion is smooth, but with F10 active it sticks/pushes against Jimmy. F10 OFF stops the forced movement. v0.10 has NOT been compiled/tested in game yet.

Likely cause:
- v0.9 fake guest orbit radius was 1.8 game units, INSIDE safe separation distance from Jimmy.
- v0.9 NPC started at +2.5 units, then F10 pulled it toward a guest goal only 1.8 units from Jimmy.
- Repeated position writes can fight player/NPC collision response.

Changes in v0.10:
- Fake guest orbit radius 3.6 units, slow angular speed 0.18 rad/s.
- NPC spawns at Jimmy.x + 3.75 instead of 2.5, leaving more space.
- Do not process a guest target within 2.80 units of Jimmy.
- Do not move an NPC within 2.35 units of Jimmy; Jimmy can step away so movement resumes.
- Reject a proposed XY step that would get the NPC closer than 2.35 units to Jimmy.
- Preserve v0.9 limitations: flat ground Z checks, small 0.12 XY steps, stale-packet and bad-handle checks.
- F9 NPC spawn; F10 toggles movement ON/OFF; F10 OFF is an immediate escape if interaction glitches.

Build:
1) Backup existing Bully.exe folder / saves and working dinput8.dll.
2) Upload ZIP contents to your GitHub repo, replacing the older bridge source, BUILD_WINDOWS.cmd and .github/workflows/build-bully-position.yml.
3) Actions -> Build Bully Co-op Anti-Stick NPC Movement v0.10 (x86).
4) Download artifact and put the new out/dinput8.dll next to Bully.exe in a SEPARATE test copy.
5) Settings in BullyCoop.ini:
[Network]
Enabled=1
Role=host
Port=7791
SessionCode=246813

[Experimental]
EnableNPCSpawnProbe=1
EnableRemoteMovementProbe=1

6) Run FAKE_GUEST_NEAR_JIMMY.cmd FROM THIS v0.10 ZIP, then Bully once in free roam; use OPEN, FLAT ground.
7) Press F9 once; press F10 once to enable motion; move Jimmy gently, away from NPC.
8) Verify the NPC no longer hugs/pushes Jimmy; try F10 OFF then ON separately to compare.
9) If any clipping/crash, STOP, do not save and send the fresh BullyCoop_bridge.log.

CAVEATS: still position stepping, not engine pathfinding, animation or verified two-machine co-op. Will NOT guarantee no wall clipping or physics glitches, especially on slopes or tight spaces. If NPC gets closer than safety distance, motion will freeze until Jimmy moves away. F10 disables movement but does not remove NPC from game.
