@echo off
set TARGET=SmartClip_Native.exe

echo 1. Terminating process...
taskkill /F /IM %TARGET% 2>nul || echo Process not running

echo 2. Cleaning files...
mingw32-make clean

echo 3. Compiling program...
mingw32-make
if %errorlevel% neq 0 (
    echo Compilation failed!
    pause
    exit /b 1
)

echo 4. Running program...
start "" %TARGET%
echo Program started

pause