param(
    [string]$Compiler = 'g++',
    [string]$Python = 'python',
    [string]$Node = 'node',
    [string]$Filter = '*',
    [switch]$SkipPython,
    [switch]$SkipNode
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$TestOutput = Join-Path $env:TEMP ('llc-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TestOutput | Out-Null

# Extra sources / libraries required by specific C++ tests (all others are header-only).
$ExtraSources = @{
    'archive'            = @('manager/lzma/lzma_all.cpp')
    'first_activation'   = @('jni/core/json_loader.cpp', 'jni/core/storage.cpp', 'manager/lzma/lzma_all.cpp')
    'manager_ui'         = @('manager/lzma/lzma_all.cpp')
    'resource_manager'   = @('manager/lzma/lzma_all.cpp')
    'structured_content' = @('jni/core/storage.cpp')
    'json_loader'        = @('jni/core/json_loader.cpp', 'jni/core/storage.cpp')
    'resource_download'  = @('jni/core/json_loader.cpp', 'jni/core/storage.cpp', 'manager/lzma/lzma_all.cpp')
}
$ExtraLibraries = @{
    'archive'           = @('-lz')
    'first_activation'  = @('-lz')
    'resource_download' = @('-lz')
    'resource_manager'  = @('-lz')
    'manager_ui'        = @('-lz')
    'pack_transaction'  = @('-lz')
}

# These tests use POSIX APIs (fork/wait, ...) and must be built on Linux; on Windows they run inside WSL.
$PosixOnly = @('archive', 'first_activation', 'manager_ui', 'pack_transaction', 'reload', 'resource_download', 'resource_manager', 'update_plan')
$IsWindowsHost = ($env:OS -eq 'Windows_NT')
$WslAvailable = $false
# These tests hardcode /data/local/tmp (Android layout); they need that directory to be writable.
$NeedsDeviceTmp = @('manager_ui', 'resource_manager')
# Needs a real release archive + metadata and an isolated /data/local/tmp; run separately on Linux/Android.
$ExternalFixtureTests = @('resource_download')
if ($IsWindowsHost -and (Get-Command wsl -ErrorAction SilentlyContinue)) {
    & wsl which g++ *> $null
    $WslAvailable = ($LASTEXITCODE -eq 0)
}

$Failures = New-Object System.Collections.Generic.List[string]
$Passed = 0

function Invoke-Suite([string]$Name, [scriptblock]$Body) {
    Write-Host "== $Name"
    try {
        & $Body
        if ($LASTEXITCODE -ne 0) { throw "exit code $LASTEXITCODE" }
        $script:Passed++
    } catch {
        Write-Host "FAILED: $Name ($_)" -ForegroundColor Red
        $script:Failures.Add($Name)
    }
}

# 1. C++ host tests: discovered automatically from tests/*_test.cpp
foreach ($File in Get-ChildItem -Path $PSScriptRoot -Filter '*_test.cpp' | Sort-Object Name) {
    $TestName = $File.BaseName -replace '_test$', ''
    if ($TestName -notlike $Filter) { continue }
    if ($ExternalFixtureTests -contains $TestName) { Write-Host "== cpp:$TestName SKIPPED (needs real release archive and metadata arguments)" -ForegroundColor Yellow; continue }
    if ($IsWindowsHost -and ($PosixOnly -contains $TestName)) {
        if (-not $WslAvailable) { Write-Host "== cpp:$TestName SKIPPED (needs WSL with g++)" -ForegroundColor Yellow; continue }
        if ($NeedsDeviceTmp -contains $TestName) {
            & wsl test -w /data/local/tmp *> $null
            if ($LASTEXITCODE -ne 0) { Write-Host "== cpp:$TestName SKIPPED (needs writable /data/local/tmp; run on device or create it in WSL)" -ForegroundColor Yellow; continue }
        }
        Invoke-Suite "cpp(wsl):$TestName" {
            $Wsl = { param($p) $p = [string]$p; "/mnt/" + $p.Substring(0,1).ToLower() + ($p.Substring(2) -replace "\\", "/") }
            $Src = @(& $Wsl $File.FullName)
            if ($ExtraSources.ContainsKey($TestName)) { $Src += $ExtraSources[$TestName] | ForEach-Object { & $Wsl (Join-Path $ProjectRoot $_) } }
            $Inc1 = & $Wsl (Join-Path $ProjectRoot 'tests/stubs')
            $Inc2 = & $Wsl (Join-Path $ProjectRoot 'jni/core')
            $Inc3 = & $Wsl (Join-Path $ProjectRoot 'manager')
            $Libs = @(); if ($ExtraLibraries.ContainsKey($TestName)) { $Libs = $ExtraLibraries[$TestName] }
            $Out = "/tmp/llc-test-$TestName"
            & wsl g++ -std=c++17 -Wall -Wextra -Werror -I $Inc1 -I $Inc2 -I $Inc3 $Src @Libs -o $Out
            if ($LASTEXITCODE -ne 0) { throw 'compilation failed' }
            & wsl $Out
        }
        continue
    }
    Invoke-Suite "cpp:$TestName" {
        $Executable = Join-Path $TestOutput ($TestName + '.exe')
        $Sources = @($File.FullName)
        if ($ExtraSources.ContainsKey($TestName)) {
            $Sources += $ExtraSources[$TestName] | ForEach-Object { Join-Path $ProjectRoot $_ }
        }
        $Libraries = @()
        if ($ExtraLibraries.ContainsKey($TestName)) { $Libraries = $ExtraLibraries[$TestName] }
        & $Compiler -std=c++17 -Wall -Wextra -Werror -I (Join-Path $ProjectRoot 'tests/stubs') -I (Join-Path $ProjectRoot 'jni/core') -I (Join-Path $ProjectRoot 'manager') @Sources @Libraries -o $Executable
        if ($LASTEXITCODE -ne 0) { throw 'compilation failed' }
        & $Executable
    }
}

# 2. Python tests
if (-not $SkipPython) {
    foreach ($File in Get-ChildItem -Path $PSScriptRoot -Filter 'test_*.py' | Sort-Object Name) {
        if ($File.BaseName -notlike "*$Filter*" -and $Filter -ne '*') { continue }
        Invoke-Suite "py:$($File.BaseName)" { $env:PYTHONPATH = $ProjectRoot; & $Python $File.FullName }
    }
}

# 3. Node tests (webui_*_test.mjs)
if (-not $SkipNode) {
    foreach ($File in Get-ChildItem -Path $PSScriptRoot -Filter '*_test.mjs' | Sort-Object Name) {
        if ($File.BaseName -notlike "*$Filter*" -and $Filter -ne '*') { continue }
        Invoke-Suite "node:$($File.BaseName)" { & $Node --test $File.FullName }
    }
}

$ResolvedTestOutput = (Resolve-Path -LiteralPath $TestOutput).ProviderPath
$ExpectedTestParent = (Resolve-Path -LiteralPath $env:TEMP).ProviderPath.TrimEnd('\', '/')
if ((Split-Path -Parent $ResolvedTestOutput) -ne $ExpectedTestParent -or
    (Split-Path -Leaf $ResolvedTestOutput) -notmatch '^llc-tests-[a-f0-9]{32}$' -or
    ((Get-Item -LiteralPath $ResolvedTestOutput).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw "Refusing to remove an unexpected test output directory: $ResolvedTestOutput"
}
Remove-Item -LiteralPath $ResolvedTestOutput -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "Passed suites: $Passed, failed: $($Failures.Count)"
if ($Failures.Count -gt 0) {
    $Failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
