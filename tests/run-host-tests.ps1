param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$TestOutput = Join-Path $env:TEMP ('llc-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TestOutput | Out-Null
foreach ($TestName in @('content_sync', 'managed_fields', 'scoped_content', 'story_sync')) {
    $Executable = Join-Path $TestOutput ($TestName + '.exe')
    & $Compiler -std=c++17 -Wall -Wextra -Werror -I (Join-Path $ProjectRoot 'tests/stubs') -I (Join-Path $ProjectRoot 'jni/core') (Join-Path $PSScriptRoot ($TestName + '_test.cpp')) -o $Executable
    if ($LASTEXITCODE -ne 0) { throw "$TestName compilation failed" }
    & $Executable
    if ($LASTEXITCODE -ne 0) { throw "$TestName regression failed" }
}
