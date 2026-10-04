Bully Co-op Hamdan v0.16 — LOCAL LOOPBACK / TWO WINDOWS (SOURCE ONLY)

This tests TWO REAL Bully.exe processes on the SAME Windows PC. It is NOT internet multiplayer, and the second visual Jimmy is still an NPC driven by another game's location updates (not full controllable co-op).

IMPORTANT game limitation: Bully might refuse to launch a second simultaneous instance. Also Bully might pause when losing focus, so background host physics may not advance even though the mod is allowed to issue tasks. This is NOT confirmed to work on Windows/game yet; do NOT circumvent DRM or existing executable protections.

SETUP:
1. Preserve your original game / saves; make TWO independent test game directories, e.g. Bully_HOST and Bully_GUEST.
2. Upload extracted source ZIP to GitHub repo; Actions -> Build Bully Co-op LOOPBACK v0.16 (x86); get the x86 out/dinput8.dll artifact.
3. Put SAME new dinput8.dll next to Bully.exe in BOTH game directories.
4. Copy HOST_COPY/BullyCoop.ini beside HOST Bully.exe; copy GUEST_COPY/BullyCoop.ini beside GUEST Bully.exe. Verify role and localhost address before opening games.
5. Close FAKE_GUEST cmd and any earlier Bully processes. Start HOST game, load free roam in an OPEN FLAT place.
6. Start GUEST game from GUEST directory. If Windows/game refuses a second simultaneous game process, stop and tell us; this test requires two processes.
7. Bring HOST window to front, press F9 ONCE to spawn an NPC. Press F10 ONCE to start host auto walking. Do NOT press F9/F10 in GUEST.
8. Bring GUEST window to front. Move guest Jimmy for 15-20 seconds in an OPEN FLAT area. Host accepts only guest UDP from 127.0.0.1; automatic NPC walk messages are allowed while host unfocused.
9. Bring HOST to front to see NPC. Note: on unfocused/pause-prone games, you may only see motion after bringing HOST to front.
10. After test, HOST F10 OFF stops issuing NEW tasks, not already active task. Close both games WITHOUT saving.
11. Send HOST and GUEST BullyCoop_bridge.log (TWO separate files, one from each directory), describe both windows and how NPC moved.

SAFETY: Never test on sole game installation; no save, use only open flat terrain, stop immediately if Jimmy original is affected, clips terrain or crash. Back up your working v0.15c DLL. No teleport/direct position writes. This tests LOOPBACK only, no internet-facing host. After success, LAN and actual online require further work.

NETWORK DETAILS:
- HOST binds ONLY to loopback 127.0.0.1:7791 when AllowBackgroundHostWalk=1.
- GUEST uses HostAddress=127.0.0.1 with ephemeral UDP source port.
- No FAKE_GUEST. Both game windows need independent processes and configs.
- This version runs safe engine-task walks on background host when guest window is foreground; game may itself pause background simulation.
- F9/F10 hotkeys always require host window focused, so guest keypresses won't accidentally toggle host.
- Do not enable AllowBackgroundHostWalk for play across two PCs; this host option forces loopback-only binding.
- Platform not tested; Windows x86 compilation & full gameplay need user's GitHub Actions and manual tests.
