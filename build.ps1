param (
    [string]$TargetSerial = "",
    [switch]$Install,
    [switch]$CoreOnly
)

$ErrorActionPreference = "Stop"

if ($Install -and $CoreOnly) {
    throw "-Install cannot be combined with -CoreOnly."
}

$ProjectRoot = $PSScriptRoot
$NdkClang = ""
if ($env:ANDROID_NDK_HOME) {
    $NdkClang = Join-Path $env:ANDROID_NDK_HOME "toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android26-clang++.cmd"
} elseif ($env:ANDROID_NDK_ROOT) {
    $NdkClang = Join-Path $env:ANDROID_NDK_ROOT "toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android26-clang++.cmd"
} else {
    $ndkRoot = Join-Path $env:LOCALAPPDATA "Android\Sdk\ndk"
    if (Test-Path $ndkRoot) {
        $foundClang = Get-ChildItem -Path $ndkRoot -Filter "aarch64-linux-android26-clang++.cmd" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
        if ($foundClang) { $NdkClang = $foundClang }
    }
}

$AdbExe = (Get-Command adb -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
if (-not $AdbExe -or -not (Test-Path $AdbExe)) {
    $defaultAdb = Join-Path $env:LOCALAPPDATA "Android\Sdk\platform-tools\adb.exe"
    if (Test-Path $defaultAdb) { $AdbExe = $defaultAdb }
}

if (-not $TargetSerial -and (Test-Path $AdbExe)) {
    $rawDevices = @(& $AdbExe devices | Where-Object { $_ -match '\tdevice$' })
    if ($rawDevices.Count -ge 1) {
        $TargetSerial = ($rawDevices[0] -split '\t')[0].Trim()
    }
}

$JniDir = Join-Path $ProjectRoot "jni"
$CoreDir = Join-Path $JniDir "core"
$ModuleDir = Join-Path $ProjectRoot "module"
$DistDir = Join-Path $ProjectRoot "dist"
$ZygiskOutDir = Join-Path $ModuleDir "zygisk"

if (-not (Test-Path $DistDir)) { New-Item -ItemType Directory -Path $DistDir -Force | Out-Null }
if (-not (Test-Path $ZygiskOutDir)) { New-Item -ItemType Directory -Path $ZygiskOutDir -Force | Out-Null }

$BuildInfo = & (Join-Path $ProjectRoot 'tools/build-info.ps1') -OutputDirectory (Join-Path $DistDir 'build-info')
$ModuleVersion = $BuildInfo.Version
$ModuleVersionCode = $BuildInfo.VersionCode
$CoreSoPath = Join-Path $DistDir "libllc_core.so"
$CoreSources = @(
    (Join-Path $CoreDir "storage.cpp"),
    (Join-Path $CoreDir "json_loader.cpp"),
    (Join-Path $CoreDir "font_injector.cpp"),
    (Join-Path $CoreDir "data_patcher.cpp"),
    (Join-Path $CoreDir "story_hook.cpp"),
    (Join-Path $CoreDir "core_main.cpp")
)

Write-Host ">>> Compiling Stage 2: libllc_core.so (Modular Data-Only)..." -ForegroundColor Cyan
& $NdkClang -shared -fPIC -std=c++17 -O3 -Wall -Wextra -I "$CoreDir" -I $BuildInfo.Directory $CoreSources -llog -ldl -o "$CoreSoPath"
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

    & (Join-Path $ProjectRoot "tools/build-manager.ps1") -Compiler $NdkClang -BuildInfoDirectory $BuildInfo.Directory
    if ($LASTEXITCODE -ne 0) { throw "Resource manager compilation failed" }

    Write-Host ">>> Synchronizing Module Metadata & Packaging Flashable Zip..." -ForegroundColor Cyan

    $ModulePropPath = Join-Path $ModuleDir 'module.prop'
    if (-not (Test-Path $ModulePropPath)) { throw 'module.prop not found in module directory' }

    Write-Host ">>> Target Module: $ModuleVersion (versionCode: $ModuleVersionCode)" -ForegroundColor Magenta

    # Package from a staging copy so the tracked module/module.prop is never modified by a build.
    $StageDir = Join-Path $DistDir 'stage'
    if (Test-Path -LiteralPath $StageDir) { Remove-Item -Recurse -Force -LiteralPath $StageDir }
    New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
    Copy-Item -Path (Join-Path $ModuleDir '*') -Destination $StageDir -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $ProjectRoot 'LICENSE') -Destination (Join-Path $StageDir 'LICENSE')
    Copy-Item -LiteralPath (Join-Path $ProjectRoot 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $StageDir 'THIRD_PARTY_NOTICES.md')
    $LicenseDir = Join-Path $StageDir 'licenses'
    New-Item -ItemType Directory -Path $LicenseDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $JniDir 'third_party/nlohmann/LICENSE.MIT') -Destination (Join-Path $LicenseDir 'nlohmann-json-LICENSE.MIT')
    Copy-Item -LiteralPath (Join-Path $JniDir 'zygisk.hpp') -Destination (Join-Path $LicenseDir 'zygisk.hpp')
    $StagedPropPath = Join-Path $StageDir 'module.prop'

    # Write the computed version into the staged module.prop
    $propLines = Get-Content -LiteralPath $ModulePropPath
    $newPropLines = @()
    foreach ($line in $propLines) {
        if ($line -match '^name=') {
            $newPropLines += "name=LLC Zygisk Localizer"
        } elseif ($line -match '^version=') {
            $newPropLines += "version=$ModuleVersion"
        } elseif ($line -match '^versionCode=') {
            $newPropLines += "versionCode=$ModuleVersionCode"
        } elseif ($line -match '^author=') {
            $newPropLines += "author=Linoleic, LLC Team"
        } else {
            $newPropLines += $line
        }
    }
    [System.IO.File]::WriteAllText($StagedPropPath, (($newPropLines -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))

    if ($ModuleVersion -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$') { throw 'Unsafe module version for package filename' }
    $ZipOutput = Join-Path $DistDir ("LLC-Zygisk-Localizer-" + $ModuleVersion + '.zip')
    if (Test-Path $ZipOutput) { Remove-Item -Force $ZipOutput }

    Push-Location $StageDir
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
        # adb writes progress to stderr; Windows PowerShell 5.1 would turn that into a terminating
        # error under "Stop", so rely on the exit code instead.
        $PrevEAP = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $PushOutput = & $AdbExe -s $TargetSerial push "$ZipOutput" "/sdcard/Download/LLC-Zygisk-Localizer.zip" 2>&1
        $PushExitCode = $LASTEXITCODE
        $ErrorActionPreference = $PrevEAP
        $PushOutput | ForEach-Object { Write-Host $_ }
        if ($PushExitCode -ne 0) {
            throw "ADB push failed with exit code $PushExitCode"
        }
        Write-Host "Pushed to /sdcard/Download/LLC-Zygisk-Localizer.zip" -ForegroundColor Green

        Write-Host ">>> Scheduling module install via ksud..." -ForegroundColor Cyan
        $InstallCommand = @'
su -c '/data/adb/ksu/bin/ksud module install /sdcard/Download/LLC-Zygisk-Localizer.zip; llc_install_rc=$?; echo "LLC_KSUD_EXIT:$llc_install_rc"'
'@
        $PrevEAP = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $InstallOutput = & $AdbExe -s $TargetSerial shell $InstallCommand 2>&1
        $AdbExitCode = $LASTEXITCODE
        $ErrorActionPreference = $PrevEAP
        $InstallOutput | ForEach-Object { Write-Host $_ }
        if ($AdbExitCode -ne 0) {
            throw "ADB shell failed with exit code $AdbExitCode"
        }
        $ExitMarker = $InstallOutput | Where-Object { $_ -match 'LLC_KSUD_EXIT:(\d+)' } | Select-Object -Last 1
        if (-not $ExitMarker) {
            throw "ksud install did not return an LLC_KSUD_EXIT status marker"
        }
        if ($ExitMarker -match 'LLC_KSUD_EXIT:(\d+)' -and [int]$Matches[1] -ne 0) {
            throw "ksud module install failed with exit code $($Matches[1])"
        }
        Write-Host "KernelSU accepted the install request. Follow the ksud output above: a hot install needs no reboot, otherwise reboot manually." -ForegroundColor Green
    }
}

if ($Install) {
    Write-Host ">>> Build completed and module install submitted; check the ksud output above for whether a reboot is required." -ForegroundColor Green
} elseif ($CoreOnly) {
    Write-Host ">>> Core-only local build completed; no ADB deployment was performed." -ForegroundColor Green
} else {
    Write-Host ">>> Local build and package completed; no ADB deployment was performed." -ForegroundColor Green
}
