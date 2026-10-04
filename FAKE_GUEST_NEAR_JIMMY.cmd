@echo off
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0FAKE_GUEST_NEAR_JIMMY.ps1"
pause
