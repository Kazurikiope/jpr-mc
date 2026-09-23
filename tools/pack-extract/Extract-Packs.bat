@echo off
rem Double-click this, or drag a server/world folder onto it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Extract-Packs.ps1" %*
echo.
pause
