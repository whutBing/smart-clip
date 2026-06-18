@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

echo ============================================
echo   SmartClip MSIX Packaging Script
echo ============================================
echo.

set "ROOT=%~dp0"
set "PKG_DIR=%ROOT%msix_package"
set "ASSETS_DIR=%PKG_DIR%\assets"
set "ICO_PATH=%ROOT%resources\clip.ico"
set "EXE_PATH=%ROOT%SmartClip_Native.exe"
set "MANIFEST=%ROOT%AppxManifest.xml"
set "OUTPUT_MSIX=%ROOT%SmartClip.msix"

REM ---- Step 1: Build the exe ----
echo [1/5] Building SmartClip_Native.exe...
taskkill /F /IM SmartClip_Native.exe 2>nul
mingw32-make -f Makefile
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)
echo Done.
echo.

REM ---- Step 2: Prepare package directory ----
echo [2/5] Preparing package directory...
if exist "%PKG_DIR%" rmdir /S /Q "%PKG_DIR%"
mkdir "%PKG_DIR%"
mkdir "%ASSETS_DIR%"
copy /Y "%EXE_PATH%" "%PKG_DIR%\SmartClip_Native.exe" >nul
copy /Y "%MANIFEST%" "%PKG_DIR%\AppxManifest.xml" >nul
echo Done.
echo.

REM ---- Step 3: Generate PNG assets from ICO ----
echo [3/5] Generating PNG assets from clip.ico...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Add-Type -AssemblyName System.Drawing;" ^
  "$ico = [System.Drawing.Icon]::new('%ICO_PATH%');" ^
  "$sizes = @(@(50,50,'StoreLogo'), @(150,150,'Square150x150Logo'), @(44,44,'Square44x44Logo'), @(310,150,'Wide310x150Logo'), @(620,300,'SplashScreen'));" ^
  "foreach ($s in $sizes) {" ^
  "  $bmp = [System.Drawing.Bitmap]::new($s[0], $s[1]);" ^
  "  $g = [System.Drawing.Graphics]::FromImage($bmp);" ^
  "  $g.Clear([System.Drawing.Color]::White);" ^
  "  $iconSize = [Math]::Min($s[0], $s[1]);" ^
  "  $icon = [System.Drawing.Icon]::new($ico, $iconSize, $iconSize);" ^
  "  $x = ($s[0] - $iconSize) / 2;" ^
  "  $y = ($s[1] - $iconSize) / 2;" ^
  "  $g.DrawIcon($icon, $x, $y);" ^
  "  $bmp.Save('%ASSETS_DIR%\' + $s[2] + '.png', [System.Drawing.Imaging.ImageFormat]::Png);" ^
  "  $g.Dispose(); $bmp.Dispose(); $icon.Dispose();" ^
  "}" ^
  "$ico.Dispose();" ^
  "Write-Host 'Assets generated successfully.'"
if %errorlevel% neq 0 (
    echo Asset generation failed!
    pause
    exit /b 1
)
echo Done.
echo.

REM ---- Step 4: Locate MakeAppx ----
echo [4/5] Locating MakeAppx tool...
set "MAKEAPPX="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Windows Kits\10\bin\%PROCESSOR_ARCHITECTURE%\*\makeappx.exe" 2^>nul ^| sort /R`) do (
    set "MAKEAPPX=%%i"
    goto :found
)
:found
if not defined MAKEAPPX (
    REM Try common paths
    for %%v in (10.0.22621.0 10.0.22000.0 10.0.20348.0 10.0.19041.0 10.0.18362.0 10.0.17763.0) do (
        set "test_path=%ProgramFiles(x86)%\Windows Kits\10\bin\%PROCESSOR_ARCHITECTURE%\%%v\makeappx.exe"
        if exist "!test_path!" (
            set "MAKEAPPX=!test_path!"
            goto :found2
        )
    )
)
:found2
if not defined MAKEAPPX (
    echo MakeAppx not found! Please install Windows SDK.
    echo Expected location: %ProgramFiles(x86)%\Windows Kits\10\bin\x64\^<version^>\makeappx.exe
    echo.
    echo You can manually create the package with:
    echo   makeappx pack /d "%PKG_DIR%" /p "%OUTPUT_MSIX%"
    pause
    exit /b 1
)
echo Found: %MAKEAPPX%
echo.

REM ---- Step 5: Create MSIX package ----
echo [5/5] Creating MSIX package...
"%MAKEAPPX%" pack /d "%PKG_DIR%" /p "%OUTPUT_MSIX%" /o
if %errorlevel% neq 0 (
    echo MSIX packaging failed!
    pause
    exit /b 1
)
echo Done.
echo.
echo ============================================
echo   MSIX package created: %OUTPUT_MSIX%
echo ============================================
echo.
echo NOTE: Before submitting to Microsoft Store:
echo   1. Update Publisher in AppxManifest.xml to match your Partner Center account
echo   2. Sign the package with your code signing certificate
echo   3. Upload the .msix file to Partner Center
echo.
pause
