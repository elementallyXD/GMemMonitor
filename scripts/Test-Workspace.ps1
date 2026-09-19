param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string]$Configuration = 'All',
    [switch]$SkipPortableDryRun,
    [switch]$SkipCodeAnalysis
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$solution = Join-Path $workspace 'GmemMonitor\GmemMonitor.slnx'
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    }
}
if (-not $msbuild -or -not (Test-Path -LiteralPath $msbuild)) { throw 'A compatible Visual Studio MSBuild installation was not found.' }
$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }
foreach ($buildConfiguration in $configurations) {
    & $msbuild $solution /m /nr:false "/p:Configuration=$buildConfiguration" /p:Platform=x64 /p:TreatWarningAsError=true /clp:ErrorsOnly
    if ($LASTEXITCODE -ne 0) { throw "$buildConfiguration x64 solution build failed." }
}

if (-not $SkipCodeAnalysis -and $configurations -contains 'Release') {
    $analysisProjects = @(
        (Join-Path $workspace 'src\core\GMemMonitor.Core.vcxproj'),
        (Join-Path $workspace 'tests\unit\GMemMonitor.MonitoringTests.vcxproj')
    )
    foreach ($analysisProject in $analysisProjects) {
        & $msbuild $analysisProject /t:Rebuild /m /nr:false /p:Configuration=Release /p:Platform=x64 `
            /p:BuildProjectReferences=false /p:TreatWarningAsError=true `
            /p:RunCodeAnalysis=true /p:EnableCppCoreCheck=true /p:CodeAnalysisTreatWarningsAsErrors=true `
            /clp:ErrorsOnly
        if ($LASTEXITCODE -ne 0) { throw "Static analysis failed for $analysisProject." }
    }
}

if ($configurations -contains 'Debug') {
    $tests = Join-Path $workspace 'GmemMonitor\x64\Debug\GMemMonitor.MonitoringTests.exe'
    $probe = Join-Path $workspace 'GmemMonitor\bin\x64\Debug\GmgnContractProbe.exe'
    if (-not (Test-Path -LiteralPath $tests) -or -not (Test-Path -LiteralPath $probe)) {
        throw 'Offline verification executables were not produced.'
    }
    & $tests
    if ($LASTEXITCODE -ne 0) { throw 'Offline unit, contract, or process tests failed.' }
    & $probe --self-test
    if ($LASTEXITCODE -ne 0) { throw 'GMGN contract probe self-test failed.' }
}

$trackedTextExtensions = @('.cpp', '.h', '.hpp', '.idl', '.xaml', '.xml', '.json', '.md', '.ps1', '.yml', '.yaml', '.txt', '.config', '.manifest')
$trackedTextFiles = git -C $workspace ls-files --cached --others --exclude-standard | Sort-Object -Unique | Where-Object {
    $trackedTextExtensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant()
} | ForEach-Object { Join-Path $workspace $_ } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
$patterns = '(GMGN_API_KEY\s*=\s*[^\s"'']{12,}|GMGN_PRIVATE_KEY\s*=\s*[^\s"'']{12,}|-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----|AKIA[0-9A-Z]{16})'
$matches = $trackedTextFiles | Select-String -Pattern $patterns | Where-Object {
    $_.Line -notmatch 'gmgn_your_actual_key|full_private_key_body|\$secretPattern|\$patterns'
}
if ($matches) { throw ('Potential credential material found in tracked files: ' + (($matches.Path | Select-Object -Unique) -join ', ')) }

if (-not $SkipPortableDryRun -and $configurations -contains 'Release') {
    & (Join-Path $PSScriptRoot 'Stage-PortableRelease.ps1') -Version '0.0.0' -DryRun
    if ($LASTEXITCODE -ne 0) { throw 'Portable release staging dry run failed.' }
}

$analysisStatus = if (-not $SkipCodeAnalysis -and $configurations -contains 'Release') { ', static analysis' } else { '' }
Write-Output "Verification passed: $($configurations -join ' and ') x64 warning-clean build$analysisStatus, offline tests, credential scan, and applicable release checks."
