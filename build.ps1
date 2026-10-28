param (
    [string]$TargetSerial = "adb-e3a6ab00-791P2g._adb-tls-connect._tcp",
    [switch]$Install,
    [switch]$CoreOnly
)

$ErrorActionPreference = "Stop"

$ProjectRoot = $PSScriptRoot
$NdkClang = "C:\Users\user\AppData\Local\Android\Sdk\ndk\26.3.11579264\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android26-clang++.cmd"
$AdbExe = "C:\Users\user\AppData\Local\Android\Sdk\platform-tools\adb.exe"

$JniDir = Join-Path $ProjectRoot "jni"
$CoreDir = Join-Path $JniDir "core"
$ModuleDir = Join-Path $ProjectRoot "module"
$DistDir = Join-Path $ProjectRoot "dist"
$ZygiskOutDir = Join-Path $ModuleDir "zygisk"

if (-not (Test-Path $DistDir)) { New-Item -ItemType Directory -Path $DistDir -Force | Out-Null }
if (-not (Test-Path $ZygiskOutDir)) { New-Item -ItemType Directory -Path $ZygiskOutDir -Force | Out-Null }

$CoreSoPath = Join-Path $DistDir "libllc_core.so"
$CoreSources = @(
    (Join-Path $CoreDir "storage.cpp"),
    (Join-Path $CoreDir "json_loader.cpp"),
    (Join-Path $CoreDir "font_injector.cpp"),
    (Join-Path $CoreDir "data_patcher.cpp"),
    (Join-Path $CoreDir "And64InlineHook.cpp"),
    (Join-Path $CoreDir "story_hook.cpp"),
    (Join-Path $CoreDir "core_main.cpp")
)

Write-Host ">>> Compiling Stage 2: libllc_core.so (Modular Data-Only)..." -ForegroundColor Cyan
& $NdkClang -shared -fPIC -std=c++17 -O3 -Wall -Wextra -I "$CoreDir" $CoreSources -llog -ldl -o "$CoreSoPath"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Core compilation failed with code $LASTEXITCODE"
    exit 1
}
Write-Host "Compiled Core: $CoreSoPath" -ForegroundColor Green

# Copy core to module for built-in package fallback
Copy-Item $CoreSoPath (Join-Path $ModuleDir "libllc_core.so") -Force

if (-not $CoreOnly) {
    Write-Host ">>> Compiling Stage 1: Zygisk arm64-v8a.so..." -ForegroundColor Cyan
    $ZygiskSoPath = Join-Path $ZygiskOutDir "arm64-v8a.so"
    $MainCpp = Join-Path $JniDir "main.cpp"

    & $NdkClang -shared -fPIC -std=c++17 -O3 -Wall -Wextra -I "$JniDir" "$MainCpp" -llog -ldl -o "$ZygiskSoPath"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Zygisk compilation failed with code $LASTEXITCODE"
        exit 1
    }
    Write-Host "Compiled Zygisk: $ZygiskSoPath" -ForegroundColor Green

    Write-Host ">>> Packaging Flashable Zip..." -ForegroundColor Cyan
    $ZipOutput = Join-Path $DistDir "LLC-Zygisk-Localizer-v0.2.0.zip"
    if (Test-Path $ZipOutput) { Remove-Item -Force $ZipOutput }

    Push-Location $ModuleDir
    try {
        & tar.exe -a -c -f "$ZipOutput" *
    } finally {
        Pop-Location
    }
    Write-Host "Packaged: $ZipOutput" -ForegroundColor Green

    Write-Host ">>> Deploying Flashable Zip to /sdcard/Download/..." -ForegroundColor Cyan
    & $AdbExe -s $TargetSerial push "$ZipOutput" "/sdcard/Download/LLC-Zygisk-Localizer.zip"
    Write-Host "Pushed to /sdcard/Download/LLC-Zygisk-Localizer.zip" -ForegroundColor Green

    if ($Install) {
        Write-Host ">>> Installing module via ksud..." -ForegroundColor Cyan
        & $AdbExe -s $TargetSerial shell "su -c '/data/adb/ksu/bin/ksud module install /sdcard/Download/LLC-Zygisk-Localizer.zip'"
        & $AdbExe -s $TargetSerial shell "su -c 'cp -r /data/adb/modules_update/llc_zygisk /data/adb/modules/ && chmod -R 755 /data/adb/modules/llc_zygisk && rm -f /data/adb/modules/llc_zygisk/update /data/adb/modules_update/llc_zygisk/update'"
        Write-Host "Module installed into KernelSU successfully! (Copied live to /data/adb/modules/llc_zygisk)" -ForegroundColor Green
    }
}

Write-Host ">>> Deploying Core to app cache with dynamic UID..." -ForegroundColor Cyan
& $AdbExe -s $TargetSerial push "$CoreSoPath" "/data/local/tmp/libllc_core.so"
$DeployCmd = @'
su -c 'TARGET_UID=$(stat -c %u /data/data/com.ProjectMoon.LimbusCompany); mkdir -p /data/local/tmp/llc && cp /data/local/tmp/libllc_core.so /data/local/tmp/llc/libllc_core.so && chmod 777 /data/local/tmp/llc/libllc_core.so && mkdir -p /data/data/com.ProjectMoon.LimbusCompany/cache && cp /data/local/tmp/libllc_core.so /data/data/com.ProjectMoon.LimbusCompany/cache/libllc_core.so && chown $TARGET_UID:$TARGET_UID /data/data/com.ProjectMoon.LimbusCompany/cache/libllc_core.so && chmod 755 /data/data/com.ProjectMoon.LimbusCompany/cache/libllc_core.so && cp /data/local/tmp/libllc_core.so /data/adb/modules/llc_zygisk/libllc_core.so 2>/dev/null; true'
'@
& $AdbExe -s $TargetSerial shell $DeployCmd
Write-Host "Core deployed to app cache (UID match) and module!" -ForegroundColor Green

Write-Host ">>> Build and deployment completed successfully." -ForegroundColor Green
