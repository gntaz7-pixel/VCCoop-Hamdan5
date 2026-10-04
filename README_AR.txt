Bully Co-op Hamdan v0.15 - Smoother Auto Walk (SOURCE ONLY, NOT COMPILED)

WHAT USER TESTED:
- v0.13: F9 spawned a second NPC; each F10 issued one walk task and NPC walked briefly.
- v0.14: F10 auto-reissued walk tasks every >=2800ms; user OBSERVED start-stop-start-stop.
- Log shows recurring AUTO WALK TASK attached=1, so network and task dispatch are working.

WHAT CHANGED IN v0.15:
1) The minimum time between NPC walk tasks is now DEFAULT 1300 ms instead of fixed 2800 ms.
2) Configurable [Experimental] WalkTaskIntervalMs from 900 to 2800 (clamped in code).
3) Near-destination no-op threshold lowered from 1.10 to 0.75 game coordinate units.
4) Same NPC identity safety checks, Jimmy clearances, flat Z check, and game-thread execution.
5) No force-teleport, no direct coordinate writes. One NPC only.
6) If task updates begin interrupting animation, raise interval to 1700 or 2200.

These are HYPOTHESES / runtime smoothing test; we have NOT observed a successful v0.15 game run.
Repeated tasks are still potentially unsafe; no proven task lifetime cleanup and no true continuous path navigation.

HOW TO TEST:
- BACK UP the last known working dinput8.dll and use a SEPARATE copied Bully folder.
- Replace GitHub repository files with this ZIP's contents, run Action:
  Build Bully Co-op Smoother Auto Walk v0.15 (x86)
- Replace only the TEST copy dinput8.dll with Actions build output.
- Keep your previous BullyCoop.ini [Network] and enabled NPC settings, ADD:
  WalkTaskIntervalMs=1300 under [Experimental].
- Start Bully as HOST in an open flat area. Launch THIS PACKAGE's FAKE_GUEST_NEAR_JIMMY.cmd.
- Wait for HOST FOUND!  F9 once creates second NPC; F10 once enables auto walk;
  monitor 10-15 seconds, press F10 again to stop NEW walk tasks.
- If motion still pauses, do not blindly lower interval; report logs/video.
- If movement gets jerky try WalkTaskIntervalMs=1800 and restart game.
- STOP immediately for Jimmy involuntary movement, wall/ground clipping or crash.
- Do not save the game during test.

INI example:
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

This is an offline reverse-engineering prototype for matching Bully.exe ONLY,
not real two-player playable co-op. No quest synchronization, combat sync, saves,
asset sync, collision guarantees, or secure internet networking.
