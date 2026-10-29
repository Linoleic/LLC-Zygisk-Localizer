param(
    [string]$Compiler = "C:\Users\user\AppData\Local\Android\Sdk\ndk\26.3.11579264\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android26-clang++.cmd",
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SourcePath = Join-Path $PSScriptRoot "resource_manager.cpp"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $ProjectRoot "module\bin\llc-manager-native"
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

& $Compiler -std=c++17 -O2 -static-libstdc++ -Wall -Wextra -Werror "$SourcePath" -llog -o "$OutputPath"
if ($LASTEXITCODE -ne 0) {
    Write-Error "管理器编译失败，退出码：$LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Output "已构建管理器：$OutputPath"
