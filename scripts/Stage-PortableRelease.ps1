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
$artifactsRoot = Join-Path $workspace 'artifacts'
$stageRoot = Join-Path $artifactsRoot ("GMemMonitor-$Version-win-x64")

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must use major.minor.patch format.' }
if ($manifest.schema_version -ne 1) { throw 'Unsupported runtime manifest schema.' }
if (-not (Test-Path -LiteralPath (Join-Path $appOutput 'GmemMonitor.exe'))) {
    throw 'Release application output is missing. Run the Release build first.'
}
$notificationResource = Join-Path $appOutput 'Microsoft.WindowsAppRuntime.Insights.Resource.dll'
if (-not (Test-Path -LiteralPath $notificationResource -PathType Leaf)) {
    throw 'The Windows App SDK notification resource DLL is missing from Release output.'
}

function Resolve-RuntimePath([string]$relativePath) {
    if (-not $relativePath.StartsWith('runtime/', [StringComparison]::Ordinal)) {
        throw "Runtime manifest path is outside runtime/: $relativePath"
    }
    $candidate = [IO.Path]::GetFullPath((Join-Path $workspace $relativePath.Replace('/', '\')))
    $expectedRoot = [IO.Path]::GetFullPath($runtimeRoot) + [IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Runtime manifest path escapes runtime/: $relativePath"
    }
    return $candidate
}

function Assert-FileHash([string]$path, [string]$expected, [string]$label) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "$label is missing: $path" }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if ($actual -ne $expected) { throw "$label hash does not match the pinned manifest." }
}

function Get-RuntimeTreeHash([string]$treeRoot) {
    $root = (Resolve-Path -LiteralPath $treeRoot).Path
    [string[]]$entries = @(Get-ChildItem -LiteralPath $root -File -Recurse -Force | ForEach-Object {
        $relative = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$relative`0$hash"
    })
    [Array]::Sort($entries, [StringComparer]::Ordinal)
    $text = [string]::Join("`n", $entries)
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

$nodePath = Resolve-RuntimePath $manifest.node.expected_relative_path
$cliEntry = Resolve-RuntimePath $manifest.gmgn_cli.expected_relative_entry
$cliPackageJson = Resolve-RuntimePath $manifest.gmgn_cli.package_json
$cliLockFile = Resolve-RuntimePath $manifest.gmgn_cli.lock_file
Assert-FileHash $nodePath $manifest.node.sha256 'Pinned Node executable'
Assert-FileHash $cliEntry $manifest.gmgn_cli.entry_sha256 'Pinned gmgn-cli entry point'
Assert-FileHash $cliLockFile $manifest.gmgn_cli.lock_file_sha256 'Pinned npm lock file'

$nodeVersion = (& $nodePath --version).TrimStart('v')
if ($LASTEXITCODE -ne 0 -or $nodeVersion -ne $manifest.node.version) { throw 'Bundled Node version does not match the manifest.' }
$cliPackage = Get-Content -LiteralPath $cliPackageJson -Raw | ConvertFrom-Json
if ($cliPackage.name -ne $manifest.gmgn_cli.package -or $cliPackage.version -ne $manifest.gmgn_cli.version) {
    throw 'Bundled gmgn-cli package identity does not match the manifest.'
}
$lockText = Get-Content -LiteralPath $cliLockFile -Raw
$lockedCliMatch = [regex]::Match($lockText, '"node_modules/gmgn-cli"\s*:\s*\{(?<body>.*?)\r?\n\s*\}', [Text.RegularExpressions.RegexOptions]::Singleline)
$lockedCliBody = $lockedCliMatch.Groups['body'].Value
if (-not $lockedCliMatch.Success -or
    $lockedCliBody -notmatch ('"version"\s*:\s*"' + [regex]::Escape($manifest.gmgn_cli.version) + '"') -or
    $lockedCliBody -notmatch ('"integrity"\s*:\s*"' + [regex]::Escape($manifest.gmgn_cli.integrity) + '"')) {
    throw 'npm lock data does not match the pinned gmgn-cli version and integrity.'
}
if ((Get-RuntimeTreeHash $runtimeRoot) -ne $manifest.runtime_tree_sha256) {
    throw 'Runtime tree hash does not match the pinned manifest.'
}

$allowedAppExtensions = @('.exe', '.dll', '.winmd', '.pri', '.xbf', '.json', '.mui', '.png')
$appFiles = @(Get-ChildItem -LiteralPath $appOutput -File -Recurse | Where-Object {
    $relative = $_.FullName.Substring($appOutput.Length + 1)
    -not $relative.StartsWith('AppX\', [StringComparison]::OrdinalIgnoreCase) -and
    $allowedAppExtensions -contains $_.Extension.ToLowerInvariant()
})
if (-not ($appFiles | Where-Object Name -eq 'GmemMonitor.exe')) { throw 'Allowlisted application output does not contain GmemMonitor.exe.' }
if (-not ($appFiles | Where-Object Name -eq 'Microsoft.WindowsAppRuntime.Insights.Resource.dll')) {
    throw 'Allowlisted application output does not contain the notification registration resource.'
}

if ($DryRun) {
    Write-Output "Portable release dry run passed: $($appFiles.Count) app files and a verified pinned runtime tree."
    return
}
if (Test-Path -LiteralPath $stageRoot) { throw 'Stage directory already exists; choose a new version or remove it manually after inspection.' }
$zipPath = "$stageRoot.zip"
if ((Test-Path -LiteralPath $zipPath) -or (Test-Path -LiteralPath "$zipPath.sha256")) {
    throw 'Release ZIP or checksum already exists; choose a new version or remove it manually after inspection.'
}

New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
foreach ($file in $appFiles) {
    $relative = $file.FullName.Substring($appOutput.Length + 1)
    $destination = Join-Path $stageRoot $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination
}

$runtimeStage = Join-Path $stageRoot 'runtime'
New-Item -ItemType Directory -Path $runtimeStage -Force | Out-Null
$verifiedRuntimeFiles = @(Get-ChildItem -LiteralPath $runtimeRoot -File -Recurse -Force)
foreach ($file in $verifiedRuntimeFiles) {
    $relative = $file.FullName.Substring($runtimeRoot.Length + 1)
    $destination = Join-Path $runtimeStage $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination
}
if ((Get-RuntimeTreeHash $runtimeStage) -ne $manifest.runtime_tree_sha256) {
    throw 'Staged runtime tree does not match the bundled runtime manifest.'
}

Copy-Item -LiteralPath (Join-Path $workspace 'README.md') -Destination $stageRoot
Copy-Item -LiteralPath (Join-Path $workspace 'THIRD_PARTY_NOTICES.txt') -Destination $stageRoot
Copy-Item -LiteralPath $manifestPath -Destination $stageRoot

$prohibited = Get-ChildItem -LiteralPath $stageRoot -File -Recurse | Where-Object {
    $_.Extension.ToLowerInvariant() -in @('.pdb', '.lib', '.exp', '.appx', '.appxrecipe', '.env', '.log', '.dmp') -or
    $_.FullName -match '[\\/](tests?|fixtures?|AppX)[\\/]'
}
if ($prohibited) { throw ('Prohibited release content found: ' + (($prohibited.FullName) -join ', ')) }
$secretPattern = '(GMGN_API_KEY\s*=\s*[^\s"'']{12,}|GMGN_PRIVATE_KEY\s*=\s*[^\s"'']{12,}|-----BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY-----|AKIA[0-9A-Z]{16})'
$textFiles = Get-ChildItem -LiteralPath $stageRoot -File -Recurse | Where-Object {
    -not $_.FullName.StartsWith(($runtimeStage + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase) -and
    $_.Length -le 5MB -and $_.Extension.ToLowerInvariant() -in @('.md', '.json', '.js', '.mjs', '.txt')
}
$secretMatches = $textFiles | Select-String -Pattern $secretPattern
if ($secretMatches) { throw ('Potential secret material found in staged release: ' + (($secretMatches.Path | Select-Object -Unique) -join ', ')) }

Compress-Archive -LiteralPath $stageRoot -DestinationPath $zipPath -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$zipPath.sha256" -Value "$hash  $([IO.Path]::GetFileName($zipPath))" -NoNewline
Write-Output "Created $zipPath and $zipPath.sha256"
