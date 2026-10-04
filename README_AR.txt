Bully Co-op Hamdan v0.15b - PREDICTIVE WALK (SOURCE ONLY)

OBSERVED v0.15: F9 spawned a separate NPC; F10 automatic walking tasks were attached.
In the user's newest log the NPC sometimes reached within 0.48-0.73 units
of the current guest point and the engine task was skipped because dist < 0.75.
This is an explanation for SOME pauses, not a complete proof of their cause.
Jimmy was independently controllable as observed by the user.

WHAT CHANGED:
- 10Hz guest-position history estimates actual incoming XY direction.
- Targets are predicted AHEAD in that direction by UP TO 1.8 XY units (configurable).
- WALK TASKS still use original engine PedMoveToXYZ allocator+constructor+attach,
  not PedSetPosXYZ teleports or direct memory writes.
- All previous strict NPC identity and Jimmy XY collision safety checks remain.
- If incoming guest position is stale/implausible/stationary, prediction is OFF.
- Diagnostic logs now say raw=... dest=... lead=... so timing can be checked.
- WalkTaskIntervalMs remains 1300 by default, bounded 900..2800.

BUILD/TEST (BACKUP GAME ONLY):
1. Unzip entire source to your gntaz7-pixel/VCCoop-Hamdan5 repo.
2. GitHub Actions -> Build Bully Co-op Predictive Walk v0.15b (x86).
3. Download produced dinput8.dll; keep v0.15 DLL as rollback.
4. Copy into a SEPARATE test installation of Bully: Scholarship Edition.
5. Name host ini BullyCoop.ini in the SAME folder as Bully.exe.
6. Start HOST in open flat space; launch THIS archive's fake guest .cmd;
   wait for HOST FOUND!; F9 once to spawn; F10 once for automatic walking.
7. Observe at most 20 seconds; F10 again to stop new walk instructions.
8. Upload full BullyCoop_bridge.log and describe whether walking is smoother.

EXAMPLE BullyCoop.ini:
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

WalkLeadCm=0 disables lookahead without needing to recompile.
Do not increase over 240; values outside bounds are automatically clamped.

LIMITATIONS:
- Source package only; Windows/GitHub build and in-game behavior UNTESTED.
- Task reallocation may still interrupt animations; no proven cleanup/pathfinding.
- Predictions can overshoot corners, do not test in alleys/near walls/NPC crowds.
- One experimental NPC; not controllable guest / not online multiplayer yet.
- Never save game in experimental session; stop if Jimmy involuntary movement,
  wall clipping or crash.
