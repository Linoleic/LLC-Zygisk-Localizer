param (
    [string]$TargetSerial = "adb-e3a6ab00-791P2g._adb-tls-connect._tcp",
    [switch]$Install,
    [switch]$CoreOnly
)

$ErrorActionPreference = "Stop"

if ($Install -and $CoreOnly) {
    throw "-Install cannot be combined with -CoreOnly."
}

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

    & (Join-Path $ProjectRoot "tools/build-manager.ps1") -Compiler $NdkClang
    if ($LASTEXITCODE -ne 0) { throw "Resource manager compilation failed" }

    Write-Host ">>> Packaging Flashable Zip..." -ForegroundColor Cyan
    $VersionLines = @(Get-Content -LiteralPath (Join-Path $ModuleDir 'module.prop') | Where-Object { $_ -match '^version=' })
    if ($VersionLines.Count -ne 1) { throw 'module.prop must contain exactly one version' }
    $ModuleVersion = $VersionLines[0].Substring(8).Trim()
    if ($ModuleVersion -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$') { throw 'Unsafe module version for package filename' }
    $ZipOutput = Join-Path $DistDir ("LLC-Zygisk-Localizer-" + $ModuleVersion + '.zip')
    if (Test-Path $ZipOutput) { Remove-Item -Force $ZipOutput }

    Push-Location $ModuleDir
    try {
        & tar.exe -a -c -f "$ZipOutput" *
        if ($LASTEXITCODE -ne 0) {
            throw "Packaging failed with tar exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
    Write-Host "Packaged: $ZipOutput" -ForegroundColor Green

    if ($Install) {
        Write-Host ">>> Pushing install package to /sdcard/Download/..." -ForegroundColor Cyan
        & $AdbExe -s $TargetSerial push "$ZipOutput" "/sdcard/Download/LLC-Zygisk-Localizer.zip"
        if ($LASTEXITCODE -ne 0) {
            throw "ADB push failed with exit code $LASTEXITCODE"
        }
        Write-Host "Pushed to /sdcard/Download/LLC-Zygisk-Localizer.zip" -ForegroundColor Green

        Write-Host ">>> Scheduling module install via ksud..." -ForegroundColor Cyan
        $InstallCommand = @'
su -c '/data/adb/ksu/bin/ksud module install /sdcard/Download/LLC-Zygisk-Localizer.zip; llc_install_rc=$?; printf "\nLLC_KSUD_EXIT:%s\n" "$llc_install_rc"'
'@
        $InstallOutput = & $AdbExe -s $TargetSerial shell $InstallCommand 2>&1
        $AdbExitCode = $LASTEXITCODE
        $InstallOutput | ForEach-Object { Write-Host $_ }
        if ($AdbExitCode -ne 0) {
            throw "ADB shell failed with exit code $AdbExitCode"
        }
        $ExitMarker = $InstallOutput | Where-Object { $_ -match '^LLC_KSUD_EXIT:(\d+)$' } | Select-Object -Last 1
        if (-not $ExitMarker) {
            throw "ksud install did not return an LLC_KSUD_EXIT status marker"
        }
        if ($ExitMarker -match '^LLC_KSUD_EXIT:(\d+)$' -and [int]$Matches[1] -ne 0) {
            throw "ksud module install failed with exit code $($Matches[1])"
        }
        Write-Host "KernelSU accepted the install request. Reboot the device manually to activate it." -ForegroundColor Green
    }
}

if ($Install) {
    Write-Host ">>> Build completed; the submitted module install requires a manual device reboot to activate." -ForegroundColor Green
} elseif ($CoreOnly) {
    Write-Host ">>> Core-only local build completed; no ADB deployment was performed." -ForegroundColor Green
} else {
    Write-Host ">>> Local build and package completed; no ADB deployment was performed." -ForegroundColor Green
}
