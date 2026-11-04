param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Properties = @{}
foreach ($Line in Get-Content -LiteralPath (Join-Path $RepoRoot 'module/module.prop')) {
    if ($Line -match '^([^=#]+)=(.*)$') { $Properties[$Matches[1]] = $Matches[2] }
}
$ReleaseVersion = $Properties['version']
$ReleaseCode = 0
if ($ReleaseVersion -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+$' -or
    -not [int]::TryParse($Properties['versionCode'], [ref]$ReleaseCode) -or $ReleaseCode -le 0) {
    throw 'module.prop must define an explicit vMAJOR.MINOR.PATCH and positive versionCode'
}
$Revision = (& git -C $RepoRoot rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $Revision -notmatch '^[a-f0-9]{40}$') { throw 'Cannot determine Git revision' }
$Changes = @(& git -C $RepoRoot status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine Git working tree state' }
$DirtyBuild = $Changes.Count -gt 0
$BuiltAt = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
$BuildId = $Revision.Substring(0,12) + $(if ($DirtyBuild) { '-local' } else { '' }) + '-' + $BuiltAt.Replace('-','').Replace(':','')
$PackageVersion = $ReleaseVersion + '-g' + $Revision.Substring(0,12) + $(if ($DirtyBuild) { '-local' } else { '' })
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$Header = @(
    '#pragma once',
    "#define LLC_RELEASE_VERSION `"$ReleaseVersion`"",
    "#define LLC_VERSION_CODE $ReleaseCode",
    "#define LLC_GIT_REVISION `"$Revision`"",
    "#define LLC_BUILD_DIRTY $(if ($DirtyBuild) { 1 } else { 0 })",
    "#define LLC_BUILD_ID `"$BuildId`"",
    "#define LLC_BUILT_AT `"$BuiltAt`""
) -join "`n"
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'llc_build_generated.hpp'), $Header + "`n", [Text.UTF8Encoding]::new($false))
[pscustomobject]@{ Version=$PackageVersion; VersionCode=$ReleaseCode; BuildId=$BuildId; Directory=$OutputDirectory }
