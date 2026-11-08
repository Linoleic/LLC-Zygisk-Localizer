param(
    [string]$TargetSerial,
    [switch]$LaunchGame,
    [int]$TimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'

function Write-Pass([string]$msg) {
    Write-Host "[PASS] $msg" -ForegroundColor Green
}

function Write-Fail([string]$msg) {
    Write-Host "[FAIL] $msg" -ForegroundColor Red
}

function Write-Info([string]$msg) {
    Write-Host "[INFO] $msg" -ForegroundColor Cyan
}

# Resolve ADB Target Device
if (-not $TargetSerial) {
    $devices = @(adb devices | Select-String -Pattern "device$" | ForEach-Object { ($_ -split "\s+")[0] })
    if ($devices.Count -eq 0) {
        Write-Fail "No connected ADB devices detected. Please connect a device or specify -TargetSerial."
        exit 1
    }
    $TargetSerial = $devices[0]
}

Write-Info "Target Device: $TargetSerial"
$adbCmd = @('adb', '-s', $TargetSerial)

# Check ADB connectivity
& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "getprop ro.product.model" | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Fail "Unable to communicate with device $TargetSerial via ADB."
    exit 1
}

$deviceModel = (& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "getprop ro.product.model").Trim()
Write-Info "Device Model: $deviceModel"

$PackageName = "com.ProjectMoon.LimbusCompany"
$GamePidRaw = (& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "pidof $PackageName").Trim()

if (-not $GamePidRaw -and $LaunchGame) {
    Write-Info "Game not running. Launching $PackageName..."
    & $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "monkey -p $PackageName -c android.intent.category.LAUNCHER 1" | Out-Null
    Start-Sleep -Seconds 8
    $GamePidRaw = (& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "pidof $PackageName").Trim()
}

if (-not $GamePidRaw) {
    Write-Fail "Game process '$PackageName' is not running on device $TargetSerial."
    exit 1
}

$GamePid = ($GamePidRaw -split "\s+")[-1]
Write-Pass "Game process running (PID: $GamePid)"

$PassCount = 1
$FailCount = 0

# Test 1: In-Game Font Asset & Fallback Diagnostics
Write-Info "Running Test 1: In-Game Font Asset and Fallback Diagnostics..."
$FontTrig = "/sdcard/Android/data/$PackageName/files/llc_font"
$FontDump = "/sdcard/Android/data/$PackageName/files/llc_font_dump.txt"

& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "rm -f $FontDump 2>/dev/null ; touch $FontTrig" | Out-Null

$fontDumpFound = $false
for ($i = 0; $i -lt $TimeoutSeconds; $i++) {
    Start-Sleep -Seconds 1
    $exists = (& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "if [ -f $FontDump ]; then echo 1; else echo 0; fi").Trim()
    if ($exists -eq "1") {
        $fontDumpFound = $true
        break
    }
}

if (-not $fontDumpFound) {
    Write-Fail "Font dump trigger did not generate $FontDump within $TimeoutSeconds seconds."
    $FailCount++
} else {
    $fontDumpContent = (& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "cat $FontDump") -join "`n"
    
    $check1 = $fontDumpContent -match "Limbus Company Font Asset & Fallback Audit"
    $check2 = $fontDumpContent -match "Name: 'SourceHanSansSC-Bold SDF'" -or $fontDumpContent -match "SourceHanSans"
    $check3 = $fontDumpContent -match "HasCharacter\(0x8FDE.*res=1, exc=0x0"
    $check4 = $fontDumpContent -match "HasCharacter\(0x6E05.*res=1, exc=0x0"
    $check5 = $fontDumpContent -match "Called ReadFontAssetDefinition\(\): exc=0x0"
    $check6 = -not ($fontDumpContent -match "<EXC:")
    $check7 = $fontDumpContent -match "SourceHanSans: YES"

    if ($check1 -and $check2 -and $check3 -and $check4 -and $check5 -and $check6 -and $check7) {
        Write-Pass "Font Asset & Fallback Audit verified: g_shs_font active, characters initialized, fallback injected, 0 exceptions."
        $PassCount++
    } else {
        Write-Fail "Font Asset & Fallback Audit failed assertions (check1=$check1, check2=$check2, check3=$check3, check4=$check4, check5=$check5, check6=$check6, check7=$check7)."
        $FailCount++
    }
}

# Test 2: Active UI Text & Tofu Box Diagnostics
Write-Info "Running Test 2: Active UI Text and Tofu Box Diagnostics..."
$TextTrig = "/sdcard/Android/data/$PackageName/files/llc_dump"
$TextDump = "/sdcard/Android/data/$PackageName/files/llc_text_dump.txt"

& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "rm -f $TextDump 2>/dev/null ; touch $TextTrig" | Out-Null

$textDumpFound = $false
for ($i = 0; $i -lt $TimeoutSeconds; $i++) {
    Start-Sleep -Seconds 1
    $exists = (& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "if [ -f $TextDump ]; then echo 1; else echo 0; fi").Trim()
    if ($exists -eq "1") {
        $textDumpFound = $true
        break
    }
}

if (-not $textDumpFound) {
    Write-Fail "Text dump trigger did not generate $TextDump within $TimeoutSeconds seconds."
    $FailCount++
} else {
    $textDumpContent = (& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "cat $TextDump") -join "`n"

    $tofuChar = [string][char]0x25A1
    $tCheck1 = $textDumpContent -match "Limbus Company Active TMP_Text Dump"
    $tCheck2 = $textDumpContent -match "Chinese \(ZH\): [1-9]"
    $tCheck3 = -not ($textDumpContent.Contains($tofuChar) -or $textDumpContent.Contains("\u25A1"))

    if ($tCheck1 -and $tCheck2 -and $tCheck3) {
        Write-Pass "Active UI Text verified: Chinese UI texts detected, zero tofu boxes."
        $PassCount++
    } else {
        Write-Fail "Active UI Text verification failed (tCheck1=$tCheck1, tCheck2=$tCheck2, tCheck3=$tCheck3)."
        $FailCount++
    }
}

# Test 3: Logcat Character Warning & Stability Check
Write-Info "Running Test 3: Logcat Character Warning and Stability Check..."
$logcatLines = (& $adbCmd[0] $adbCmd[1] $adbCmd[2] shell "logcat -d --pid=$GamePid") -join "`n"

$hasTofuWarning = $logcatLines -match "was not found in .* font asset or any potential fallbacks. It was replaced by Unicode character"
$hasFatalCrash = $logcatLines -match "Fatal signal 11 \(SIGSEGV\)" -or $logcatLines -match "Fatal signal 6 \(SIGABRT\)"

if (-not $hasTofuWarning -and -not $hasFatalCrash) {
    Write-Pass "Logcat verified: zero missing glyph warnings, process stable."
    $PassCount++
} else {
    if ($hasTofuWarning) {
        Write-Fail "Logcat contains TMPro missing character warnings!"
    }
    if ($hasFatalCrash) {
        Write-Fail "Logcat contains fatal crash signals!"
    }
    $FailCount++
}

# Summary
Write-Host "================================================================================"
Write-Host "Device Test Results on $TargetSerial ($deviceModel):"
Write-Host "Passed Checks: $PassCount | Failed Checks: $FailCount"
Write-Host "================================================================================"

if ($FailCount -eq 0) {
    exit 0
} else {
    exit 1
}
