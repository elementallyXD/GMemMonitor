param([switch]$Force)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$manifest = Get-Content -LiteralPath (Join-Path $workspace 'packaging\runtime-manifest.json') -Raw | ConvertFrom-Json
$runtimeRoot = Join-Path $workspace 'runtime'
$nodePath = Join-Path $runtimeRoot 'node.exe'
$cliRoot = Join-Path $runtimeRoot 'gmgn-cli'
$cliEntry = Join-Path $cliRoot 'node_modules\gmgn-cli\dist\index.js'
$packageSource = Join-Path $workspace 'packaging\gmgn-cli'

function Test-PinnedRuntime {
    if (-not (Test-Path -LiteralPath $nodePath -PathType Leaf) -or -not (Test-Path -LiteralPath $cliEntry -PathType Leaf)) { return $false }
    if ((Get-FileHash -LiteralPath $nodePath -Algorithm SHA256).Hash -ne $manifest.node.sha256) { return $false }
    if ((Get-FileHash -LiteralPath $cliEntry -Algorithm SHA256).Hash -ne $manifest.gmgn_cli.entry_sha256) { return $false }
    $lockPath = Join-Path $cliRoot 'package-lock.json'
    if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf) -or
        (Get-FileHash -LiteralPath $lockPath -Algorithm SHA256).Hash -ne $manifest.gmgn_cli.lock_file_sha256) { return $false }
    $package = Get-Content -LiteralPath (Join-Path $cliRoot 'node_modules\gmgn-cli\package.json') -Raw | ConvertFrom-Json
    return $package.name -eq $manifest.gmgn_cli.package -and $package.version -eq $manifest.gmgn_cli.version -and
        (Get-RuntimeTreeHash $runtimeRoot) -eq $manifest.runtime_tree_sha256
}

function Get-RuntimeTreeHash([string]$root) {
    $resolvedRoot = (Resolve-Path -LiteralPath $root).Path
    [string[]]$entries = @(Get-ChildItem -LiteralPath $resolvedRoot -File -Recurse | ForEach-Object {
        $relative = $_.FullName.Substring($resolvedRoot.Length + 1).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$relative`0$hash"
    })
    [Array]::Sort($entries, [StringComparer]::Ordinal)
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes([string]::Join("`n", $entries))
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

if ((Test-PinnedRuntime) -and -not $Force) {
    Write-Output 'Pinned runtime is already provisioned and verified.'
    return
}
if ((Test-Path -LiteralPath $runtimeRoot) -and -not $Force) {
    throw 'An incomplete or mismatched runtime directory exists. Inspect it, then rerun with -Force to replace only that runtime directory.'
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("GMemMonitor-runtime-" + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    $nodeArchive = Join-Path $temporaryRoot 'node.zip'
    Invoke-WebRequest -Uri $manifest.node.source -OutFile $nodeArchive
    $nodeExtract = Join-Path $temporaryRoot 'node'
    Expand-Archive -LiteralPath $nodeArchive -DestinationPath $nodeExtract
    $extractedNode = Get-ChildItem -LiteralPath $nodeExtract -Filter node.exe -File -Recurse | Select-Object -First 1
    if (-not $extractedNode -or (Get-FileHash -LiteralPath $extractedNode.FullName -Algorithm SHA256).Hash -ne $manifest.node.sha256) {
        throw 'Downloaded Node executable does not match the pinned manifest hash.'
    }
    $npmCli = Get-ChildItem -LiteralPath $nodeExtract -Filter npm-cli.js -File -Recurse | Select-Object -First 1
    if (-not $npmCli) { throw 'Downloaded Node archive does not contain npm-cli.js.' }

    $newRuntime = Join-Path $temporaryRoot 'runtime'
    $newCliRoot = Join-Path $newRuntime 'gmgn-cli'
    New-Item -ItemType Directory -Path $newCliRoot -Force | Out-Null
    Copy-Item -LiteralPath $extractedNode.FullName -Destination (Join-Path $newRuntime 'node.exe')
    Copy-Item -LiteralPath (Join-Path $packageSource 'package.json') -Destination $newCliRoot
    Copy-Item -LiteralPath (Join-Path $packageSource 'package-lock.json') -Destination $newCliRoot
    & $extractedNode.FullName $npmCli.FullName ci --ignore-scripts --omit=dev --prefix $newCliRoot --cache (Join-Path $temporaryRoot 'npm-cache')
    if ($LASTEXITCODE -ne 0) { throw 'npm failed to provision the pinned gmgn-cli dependency tree.' }

    $newEntry = Join-Path $newCliRoot 'node_modules\gmgn-cli\dist\index.js'
    if ((Get-FileHash -LiteralPath $newEntry -Algorithm SHA256).Hash -ne $manifest.gmgn_cli.entry_sha256) {
        throw 'Provisioned gmgn-cli entry point does not match the pinned hash.'
    }
    if ((Get-RuntimeTreeHash $newRuntime) -ne $manifest.runtime_tree_sha256) {
        throw 'Provisioned runtime tree does not match the pinned manifest hash.'
    }
    if (Test-Path -LiteralPath $runtimeRoot) { Remove-Item -LiteralPath $runtimeRoot -Recurse -Force }
    Move-Item -LiteralPath $newRuntime -Destination $runtimeRoot
    Write-Output 'Pinned Node and gmgn-cli runtime provisioned successfully.'
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) { Remove-Item -LiteralPath $temporaryRoot -Recurse -Force }
}
