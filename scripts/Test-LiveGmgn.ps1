param(
    [Parameter(Mandatory = $true)]
    [switch]$EnableLiveGmgn,
    [string]$Token
)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$probe = Join-Path $workspace 'GmemMonitor\bin\x64\Debug\GmgnContractProbe.exe'
$node = Join-Path $workspace 'runtime\node.exe'
$cli = Join-Path $workspace 'runtime\gmgn-cli\node_modules\gmgn-cli\dist\index.js'

if (-not $EnableLiveGmgn) { throw 'Live GMGN tests require the explicit -EnableLiveGmgn switch.' }
if (-not (Test-Path -LiteralPath $probe)) { throw 'Build Debug x64 before running live tests.' }
if (-not (Test-Path -LiteralPath $node) -or -not (Test-Path -LiteralPath $cli)) { throw 'Provision the pinned runtime before running live tests.' }
if ($Token -and $Token -notmatch '^0x[0-9a-fA-F]{40}$') { throw 'Token must be a public BSC contract address.' }

& $probe --node $node --cli-entry $cli --action config-check
if ($LASTEXITCODE -ne 0) {
    Write-Output 'Live GMGN tests skipped: the pinned CLI did not detect the external user configuration.'
    exit 0
}

Write-Output 'Running one credential-safe BSC BUY feed request. Raw output will not be displayed.'
& $probe --node $node --cli-entry $cli --action follow-wallet
if ($LASTEXITCODE -ne 0) { throw 'Live followed-wallet contract check failed. Do not retry during a reported cooldown.' }

if ($Token) {
    Start-Sleep -Seconds 2
    & $probe --node $node --cli-entry $cli --action token-info --token $Token
    if ($LASTEXITCODE -ne 0) { throw 'Live token-info contract check failed.' }
    Start-Sleep -Seconds 2
    & $probe --node $node --cli-entry $cli --action token-security --token $Token
    if ($LASTEXITCODE -ne 0) { throw 'Live token-security contract check failed.' }
}

Write-Output 'Opt-in live GMGN checks passed without printing raw response data.'
