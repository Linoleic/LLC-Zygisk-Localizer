param(
    [string]$Compiler = "",
    [string]$OutputPath,
    [string]$BuildInfoDirectory = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SourcePath = Join-Path $PSScriptRoot "resource_manager.cpp"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $ProjectRoot "module\bin\llc-manager-native"
}

if (-not $Compiler) {
    if ($env:ANDROID_NDK_HOME) {
        $Compiler = Join-Path $env:ANDROID_NDK_HOME "toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android26-clang++.cmd"
    } elseif ($env:ANDROID_NDK_ROOT) {
        $Compiler = Join-Path $env:ANDROID_NDK_ROOT "toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android26-clang++.cmd"
    } else {
        $ndkRoot = Join-Path $env:LOCALAPPDATA "Android\Sdk\ndk"
        if (Test-Path $ndkRoot) {
            $found = Get-ChildItem -Path $ndkRoot -Filter "aarch64-linux-android26-clang++.cmd" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
            if ($found) { $Compiler = $found }
        }
    }
}

if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    Write-Error "NDK 编译器不存在：$Compiler"
    exit 1
}
if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
    Write-Error "管理器源码不存在：$SourcePath"
    exit 1
}

$OutputDirectory = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
}

$CoreDir = Join-Path $ProjectRoot 'jni/core'
if (-not $BuildInfoDirectory) {
    $Info = & (Join-Path $PSScriptRoot 'build-info.ps1') -OutputDirectory (Join-Path $ProjectRoot 'dist/manager-build-info')
    $BuildInfoDirectory = $Info.Directory
}
if (-not (Test-Path -LiteralPath (Join-Path $BuildInfoDirectory 'llc_build_generated.hpp') -PathType Leaf)) {
    throw 'Build information header is missing'
}
& (Join-Path $PSScriptRoot 'build-https.ps1') -OutputPath (Join-Path $OutputDirectory 'llc-https.jar')
& $Compiler -std=c++17 -O2 -static-libstdc++ -Wall -Wextra -Werror -I $BuildInfoDirectory "$SourcePath" (Join-Path $CoreDir 'json_loader.cpp') (Join-Path $CoreDir 'storage.cpp') -llog -lz -o "$OutputPath"
if ($LASTEXITCODE -ne 0) {
    Write-Error "管理器编译失败，退出码：$LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Output "已构建管理器：$OutputPath"
