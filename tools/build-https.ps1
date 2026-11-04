param([string]$OutputPath = '')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$SdkRoot = Join-Path $env:LOCALAPPDATA 'Android/Sdk'
if ($env:ANDROID_SDK_ROOT) { $SdkRoot = $env:ANDROID_SDK_ROOT }
$D8 = Get-ChildItem (Join-Path $SdkRoot 'build-tools') -Filter d8.bat -Recurse | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$AndroidJar = Get-ChildItem (Join-Path $SdkRoot 'platforms') -Filter android.jar -Recurse | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (!$D8 -or !$AndroidJar) { throw 'HTTPS helper requires Android SDK build-tools and platform' }
if (!$OutputPath) { $OutputPath = Join-Path $ProjectRoot 'module/bin/llc-https.jar' }
$BuildDir = Join-Path $ProjectRoot 'dist/https-helper'
New-Item -ItemType Directory -Force (Join-Path $BuildDir 'classes'), (Join-Path $BuildDir 'dex'), (Split-Path $OutputPath -Parent) | Out-Null
& javac --release 8 -d (Join-Path $BuildDir 'classes') (Join-Path $PSScriptRoot 'https/HttpsFetch.java')
if ($LASTEXITCODE) { throw 'HTTPS Java compilation failed' }
$ClassJar = Join-Path $BuildDir 'classes.jar'
& jar --create --file $ClassJar -C (Join-Path $BuildDir 'classes') .
if ($LASTEXITCODE) { throw 'HTTPS classes packaging failed' }
& $D8 --release --min-api 26 --lib $AndroidJar --output (Join-Path $BuildDir 'dex') $ClassJar
if ($LASTEXITCODE) { throw 'HTTPS dex compilation failed' }
& jar --create --file $OutputPath -C (Join-Path $BuildDir 'dex') classes.dex
if ($LASTEXITCODE) { throw 'HTTPS dex packaging failed' }
Write-Output "HTTPS helper built: $OutputPath"
