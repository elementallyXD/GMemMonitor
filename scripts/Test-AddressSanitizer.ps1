param()

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
$visualStudioRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community'
if (-not (Test-Path -LiteralPath $msbuild)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio MSBuild was not found.' }
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    $visualStudioRoot = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
}
if (-not $msbuild -or -not (Test-Path -LiteralPath $msbuild)) { throw 'A compatible Visual Studio MSBuild installation was not found.' }

$fixtureProject = Join-Path $workspace 'tests\process-fixtures\GMemMonitor.ProcessFixtures.vcxproj'
$testProject = Join-Path $workspace 'tests\unit\GMemMonitor.MonitoringTests.vcxproj'
& $msbuild $fixtureProject /t:Build /m /nr:false /p:Configuration=Debug /p:Platform=x64 /p:TreatWarningAsError=true /clp:ErrorsOnly
if ($LASTEXITCODE -ne 0) { throw 'The process fixture build failed.' }
& $msbuild $testProject /t:Rebuild /m /nr:false /p:Configuration=Debug /p:Platform=x64 /p:TreatWarningAsError=true /p:EnableASAN=true /clp:ErrorsOnly
if ($LASTEXITCODE -ne 0) { throw 'The AddressSanitizer test build failed.' }

$asanRuntime = Get-ChildItem (Join-Path $visualStudioRoot 'VC\Tools\MSVC') -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll' } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $asanRuntime) { throw 'The x64 AddressSanitizer runtime was not found in the selected Visual Studio installation.' }

$testOutput = Join-Path $workspace 'tests\unit\x64\Debug'
$testExecutable = Join-Path $testOutput 'GMemMonitor.MonitoringTests.exe'
Copy-Item -LiteralPath $asanRuntime -Destination (Join-Path $testOutput 'clang_rt.asan_dynamic-x86_64.dll') -Force
& $testExecutable
if ($LASTEXITCODE -ne 0) { throw 'AddressSanitizer reported a failure in the offline test suite.' }
Write-Output 'AddressSanitizer verification passed.'
