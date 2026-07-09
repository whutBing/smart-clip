<#
.SYNOPSIS
  更新 SmartClip 版本号（version.h + AppxManifest.xml）

  version.h 中的 APP_VERSION_STRING 宏被以下位置引用，更新 version.h 即自动覆盖：
    - 托盘提示文本  src\tray.cpp        BuildTrayTooltip()
    - 关于页版本号  src\settings.cpp    关于分类第0行
    - 资源文件版本  resources\resource.rc  VS_VERSION_INFO

.DESCRIPTION
  用法:  powershell -ExecutionPolicy Bypass -File scripts\bump_version.ps1 0.1.6.0
         pwsh -File scripts\bump_version.ps1 0.1.7.0
#>
param(
  [Parameter(Mandatory=$true, Position=0)]
  [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
  [string]$Version
)

# --- 定位项目根目录（脚本在 scripts/ 下，根目录是上一级）---
$scriptDir = if ($PSScriptRoot) {
  $PSScriptRoot
} elseif ($MyInvocation.MyCommand.Path) {
  Split-Path -Parent $MyInvocation.MyCommand.Path
} else {
  $PWD.Path
}
$root    = Split-Path -Parent $scriptDir
$vhPath  = Join-Path $root 'include\version.h'
$appxPath = Join-Path $root 'AppxManifest.xml'

# --- 解析版本号 ---
$parts = $Version.Split('.')
$maj = $parts[0]; $mij = $parts[1]; $bld = $parts[2]; $rev = $parts[3]

Write-Host "Bump version -> $Version"
Write-Host "  version.h:     $vhPath"
Write-Host "  AppxManifest:  $appxPath"
Write-Host ""

# --- UTF-8 无 BOM 编码（避免编译器/解析器问题）---
$utf8NoBom = New-Object System.Text.UTF8Encoding $false

# === 1. 更新 version.h ===
# 托盘 (tray.cpp) 和关于页 (settings.cpp) 都通过 APP_VERSION_STRING 宏引用，
# 更新 version.h 后重新编译即自动生效。
if (-not (Test-Path $vhPath)) { Write-Error "version.h not found: $vhPath"; exit 1 }
$vh = Get-Content $vhPath -Raw -Encoding UTF8
$vh = $vh -replace '#define APP_VERSION_MAJOR \d+',    "#define APP_VERSION_MAJOR $maj"
$vh = $vh -replace '#define APP_VERSION_MINOR \d+',    "#define APP_VERSION_MINOR $mij"
$vh = $vh -replace '#define APP_VERSION_BUILD \d+',    "#define APP_VERSION_BUILD $bld"
$vh = $vh -replace '#define APP_VERSION_REVISION \d+', "#define APP_VERSION_REVISION $rev"
$vh = $vh -replace '#define APP_VERSION_STRING L"[0-9.]+"',  "#define APP_VERSION_STRING L`"$Version`""
$vh = $vh -replace '#define APP_VERSION_STRING_A "[0-9.]+"', "#define APP_VERSION_STRING_A `"$Version`""
[System.IO.File]::WriteAllText($vhPath, $vh, $utf8NoBom)
Write-Host "[OK] version.h         -> $Version   (tray.cpp / settings.cpp / resource.rc)"

# === 2. 更新 AppxManifest.xml ===
# 仅匹配 <Identity> 元素的 Version 属性（行首缩进的 Version=），
# 不影响 <TargetDeviceFamily> 的 MinVersion / MaxVersionTested。
if (-not (Test-Path $appxPath)) { Write-Error "AppxManifest.xml not found: $appxPath"; exit 1 }
$appx = Get-Content $appxPath -Raw -Encoding UTF8
# 用捕获组 $1 保留原有缩进，只替换版本号
$appx = $appx -replace '(?m)^(\s+)Version="\d+\.\d+\.\d+\.\d+"', "`$1Version=`"$Version`""
[System.IO.File]::WriteAllText($appxPath, $appx, $utf8NoBom)
Write-Host "[OK] AppxManifest.xml  -> Version=`"$Version`""

# === 3. 验证 ===
Write-Host ""
Write-Host "=== Verification ==="
$vhCheck = [System.IO.File]::ReadAllText($vhPath)
if ($vhCheck -match "APP_VERSION_STRING L`"$Version`"") {
  Write-Host "[PASS] version.h        APP_VERSION_STRING = $Version"
} else {
  Write-Warning "[FAIL] version.h verification failed!"
}
$appxCheck = [System.IO.File]::ReadAllText($appxPath)
if ($appxCheck -match "Version=`"$Version`"") {
  Write-Host "[PASS] AppxManifest.xml Version = $Version"
} else {
  Write-Warning "[FAIL] AppxManifest.xml verification failed!"
}
# 确认 MinVersion 没被误改
if ($appxCheck -match 'MinVersion="10\.0\.17763\.0"') {
  Write-Host "[PASS] MinVersion preserved (10.0.17763.0)"
} else {
  Write-Warning "[FAIL] MinVersion was accidentally modified!"
}

Write-Host ""
Write-Host "Done. Recompile to apply: tray tooltip + about page."
