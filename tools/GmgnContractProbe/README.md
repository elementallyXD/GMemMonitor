# GMGN Contract Probe

`GmgnContractProbe` is the Phase 01 console-only probe. It invokes a pinned Node.js executable and a pinned `gmgn-cli` entry script through `CreateProcessW`, with no shell and no secret arguments.

It does not read, write, display, or copy the GMGN `.env` file. The CLI is expected to load its own external configuration from `%USERPROFILE%\.config\gmgn\.env`.

## Build

Open `GmemMonitor/GmemMonitor.slnx` in Visual Studio and build the solution for
`Debug | x64`. The solution maps the probe output to
`GmemMonitor/bin/x64/Debug/`. Building the probe project by itself instead places its
output under `tools/GmgnContractProbe/bin/x64/Debug/`.

## Offline verification

From the repository root after a Debug x64 build:

```powershell
& ".\GmemMonitor\bin\x64\Debug\GmgnContractProbe.exe" --self-test
```

The self-test validates timeout parsing, BSC address validation, and the strict JSON syntax validator without contacting GMGN.

## Live verification

The probe requires absolute paths. The package itself is not committed or globally discovered by the probe. The command below matches the current development runtime layout; replace either absolute path if your machine uses a different location.

```powershell
& ".\GmemMonitor\bin\x64\Debug\GmgnContractProbe.exe" `
  --node "C:\Program Files\nodejs\node.exe" `
  --cli-entry "E:\blockchain-projects\GMemMonitor\runtime\gmgn-cli\node_modules\gmgn-cli\dist\index.js" `
  --action follow-wallet
```

Supported actions:

```text
version
config-check
follow-wallet
token-info --token 0x...
token-security --token 0x...
```

`follow-wallet` runs exactly:

```text
node.exe <pinned-cli-entry> track follow-wallet --chain bsc --side buy --limit 100 --raw
```

`config-check` is a non-network preflight. It asks the pinned CLI only whether it can
detect an externally configured API key and reports yes/no without printing or reading
the credential value. Run it successfully before a live `follow-wallet` request.

The signed command remains the full external-configuration check: it requires the
API key and signing private key to work through the CLI's external configuration, with
no credentials passed by the probe.

The probe reports only exit status, duration, bounded byte counts, and JSON syntax status. It intentionally does not print raw stdout/stderr because account activity and CLI diagnostics may be sensitive.

To capture the response shape for an offline parser test, run one live request from
the repository root and add `--sanitized-fixture` with a **new**, absolute output path:

```powershell
& ".\GmemMonitor\bin\x64\Debug\GmgnContractProbe.exe" `
  --node "C:\Program Files\nodejs\node.exe" `
  --cli-entry "E:\blockchain-projects\GMemMonitor\runtime\gmgn-cli\node_modules\gmgn-cli\dist\index.js" `
  --action follow-wallet `
  --sanitized-fixture "E:\blockchain-projects\GMemMonitor\tests\contract\fixtures\gmgn\follow_wallet_buy.json"
```

The output path is intentionally restricted to that fixture directory and the file
must not already exist. The probe keeps JSON object keys and JSON value types, but
replaces **every** value before writing the fixture. It never creates a raw-response
file. Inspect the resulting file before committing it: JSON keys must be contract
field names rather than user-controlled values, and no sensitive material may remain.

When the CLI reports `HTTP 429`/`RATE_LIMIT_EXCEEDED`, the probe reports a safe
rate-limit classification and any reported remaining cooldown without exposing raw
stderr. Stop testing until GMGN's cooldown expires; repeated requests may extend an
IP-based limit. See [human testing guidance](../../Documentation/human-testing.md).
