@echo off
setlocal
cd /d "%~dp0"
echo BloodRush_telemetry — start RoB (2D or PC-VR) in another window.
echo Live cart line updates when the emulator feeds UDP :25151
echo.
python -m bloodrush_telemetry %*
if errorlevel 1 pause
