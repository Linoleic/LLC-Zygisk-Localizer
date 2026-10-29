param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$TestOutput = Join-Path $env:TEMP ('llc-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TestOutput | Out-Null
foreach ($TestName in @('content_sync', 'managed_fields', 'scoped_content', 'story_sync', 'story_observation', 'story_refresh_guard', 'compatibility', 'translation_file', 'ui_content', 'json_loader', 'resource_pack', 'string_table_patcher')) {
    $Executable = Join-Path $TestOutput ($TestName + '.exe')
    $Sources = @((Join-Path $PSScriptRoot ($TestName + '_test.cpp')))
    if ($TestName -eq 'json_loader') {
        $Sources += (Join-Path $ProjectRoot 'jni/core/json_loader.cpp'), (Join-Path $ProjectRoot 'jni/core/storage.cpp')
    }
    & $Compiler -std=c++17 -Wall -Wextra -Werror -I (Join-Path $ProjectRoot 'tests/stubs') -I (Join-Path $ProjectRoot 'jni/core') @Sources -o $Executable
    if ($LASTEXITCODE -ne 0) { throw "$TestName compilation failed" }
    & $Executable
    if ($LASTEXITCODE -ne 0) { throw "$TestName regression failed" }
}
