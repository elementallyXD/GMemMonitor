param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$manifestPath = Join-Path $workspace 'packaging\runtime-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$appOutput = Join-Path $workspace 'GmemMonitor\x64\Release\GmemMonitor'
$runtimeRoot = Join-Path $workspace 'runtime'
$stageRoot = Join-Path $workspace ("artifacts\GMemMonitor-$Version-win-x64")

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must use major.minor.patch format.' }
if (-not (Test-Path -LiteralPath (Join-Path $appOutput 'GmemMonitor.exe'))) { throw 'Release application output is missing. Run the Release build first.' }
if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot $manifest.node.expected_relative_path.Replace('runtime/', '')))) { throw 'Pinned runtime node.exe is missing.' }
if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot $manifest.gmgn_cli.expected_relative_entry.Replace('runtime/', '')))) { throw 'Pinned gmgn-cli entry script is missing.' }

$releaseFiles = @('GmemMonitor.exe', 'README.md', 'THIRD_PARTY_NOTICES.txt', 'runtime-manifest.json')
if ($DryRun) { Write-Output ('Portable release dry run passed: ' + ($releaseFiles -join ', ')); return }
if (Test-Path -LiteralPath $stageRoot) { throw 'Stage directory already exists; choose a new version or remove it manually after inspection.' }

New-Item -ItemType Directory -Path $stageRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $appOutput 'GmemMonitor.exe') -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $workspace 'README.md') -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $workspace 'THIRD_PARTY_NOTICES.txt') -Destination $stageRoot
Copy-Item -LiteralPath $manifestPath -Destination $stageRoot
Copy-Item -LiteralPath $runtimeRoot -Destination (Join-Path $stageRoot 'runtime') -Recurse

$zipPath = "$stageRoot.zip"
Compress-Archive -LiteralPath $stageRoot -DestinationPath $zipPath -CompressionLevel Optimal
(Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash | Set-Content -LiteralPath "$zipPath.sha256" -NoNewline
Write-Output "Created $zipPath and $zipPath.sha256"
