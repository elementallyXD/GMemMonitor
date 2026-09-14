param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$solution = Join-Path $workspace 'GmemMonitor\GmemMonitor.slnx'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'

if (-not (Test-Path -LiteralPath $msbuild)) { throw 'Visual Studio MSBuild was not found at the documented location.' }
& $msbuild $solution /m "/p:Configuration=$Configuration" /p:Platform=x64 /clp:ErrorsOnly
if ($LASTEXITCODE -ne 0) { throw 'Solution build failed.' }

if ($Configuration -eq 'Debug') {
    $tests = Join-Path $workspace 'GmemMonitor\x64\Debug\GMemMonitor.MonitoringTests.exe'
    if (-not (Test-Path -LiteralPath $tests)) { throw 'Offline test executable was not produced.' }
    & $tests
    if ($LASTEXITCODE -ne 0) { throw 'Offline tests failed.' }
}

$scanRoots = @('src', 'GmemMonitor', 'Documentation', 'packaging', 'tests', 'tools') | ForEach-Object { Join-Path $workspace $_ }
$patterns = '(GMGN_API_KEY\s*=|GMGN_PRIVATE_KEY\s*=|-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----|AKIA[0-9A-Z]{16})'
$documentedPlaceholders = @(
    'GMGN_API_KEY=gmgn_your_actual_key',
    'GMGN_PRIVATE_KEY=-----BEGIN PRIVATE KEY-----\n...full_private_key_body...\n-----END PRIVATE KEY-----'
)
$matches = Get-ChildItem -LiteralPath $scanRoots -File -Recurse |
    Select-String -Pattern $patterns |
    Where-Object { $documentedPlaceholders -notcontains $_.Line.Trim() }
if ($matches) { throw ('Potential credential material found in: ' + (($matches | Select-Object -ExpandProperty Path -Unique) -join ', ')) }

Write-Output "Verification passed: $Configuration x64 build, applicable offline tests, and credential-pattern scan."
