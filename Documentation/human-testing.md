# Human Testing and GMGN Setup

This guide is for the current development state of GMemMonitor. It separates safe
offline verification from an opt-in live GMGN contract check. The WinUI dashboard is
currently a shell; clicking **Start monitoring** does not yet start live monitoring.

## Official GMGN references

- [GMGN Agent API](https://docs.gmgn.ai/index/gmgn-agent-api) — API-key setup,
  external configuration, supported chains, and IPv4-only note.
- [Generate Public Key](https://docs.gmgn.ai/index/generate-public-key) — create an
  Ed25519 key pair and upload the complete public key.
- [GMGN IP rate-limit guidance](https://docs.gmgn.ai/index/cooperation-api-data-crawling-ip-whitelist)
  — GMGN documents a default limit of one request per second and directs users who
  believe they are blocked in error to its support channel.
- [Official CLI usage](https://github.com/GMGNAI/gmgn-skills/blob/main/docs/cli-usage.md)
  and [tracking skill](https://github.com/GMGNAI/gmgn-skills/blob/main/skills/gmgn-track/SKILL.md).

## 1. Configure GMGN outside the repository

1. Generate an **Ed25519** key pair locally. Keep the whole public and private PEM
   blocks, including their `BEGIN` and `END` lines.
2. Open <https://gmgn.ai/ai>, paste only the complete public key, and create an API
   key.
3. Create or edit this user-owned file:

   ```text
   %USERPROFILE%\.config\gmgn\.env
   ```

4. Configure the exact key pair used to create that API key. The pinned CLI accepts
   a one-line private PEM with literal `\n` separators:

   ```dotenv
   GMGN_API_KEY=gmgn_your_actual_key
   GMGN_PRIVATE_KEY=-----BEGIN PRIVATE KEY-----\n...full_private_key_body...\n-----END PRIVATE KEY-----
   ```

Never put this file in the repository or share its contents. The request-signing
private key is not a blockchain wallet key and the application never reads or writes
the file directly; `gmgn-cli` loads it.

## 2. Build and run offline tests

Open **Developer PowerShell for Visual Studio** in the repository root and run:

```powershell
MSBuild .\GmemMonitor\GmemMonitor.slnx /m /p:Configuration=Debug /p:Platform=x64

& ".\GmemMonitor\bin\x64\Debug\GmgnContractProbe.exe" --self-test
& ".\GmemMonitor\x64\Debug\GMemMonitor.MonitoringTests.exe"
```

Expected output contains `Self-test passed.` and `Monitoring self-tests passed.`.
These tests do not use the network or credentials.

## 3. Verify the installed CLI and external API-key configuration without contacting GMGN

The following command only verifies that Node can launch the installed CLI:

```powershell
& ".\GmemMonitor\bin\x64\Debug\GmgnContractProbe.exe" `
  --node "C:\Program Files\nodejs\node.exe" `
  --cli-entry "E:\blockchain-projects\GMemMonitor\runtime\gmgn-cli\node_modules\gmgn-cli\dist\index.js" `
  --action version
```

Adjust the absolute paths if Node or the project is elsewhere. `--action version`
does not prove credentials or access to the followed-wallet feed.

Before any live request, run the read-only configuration preflight:

```powershell
& ".\GmemMonitor\bin\x64\Debug\GmgnContractProbe.exe" `
  --node "C:\Program Files\nodejs\node.exe" `
  --cli-entry "E:\blockchain-projects\GMemMonitor\runtime\gmgn-cli\node_modules\gmgn-cli\dist\index.js" `
  --action config-check
```

It reports only whether the pinned CLI can detect `GMGN_API_KEY`; it does not print,
copy, or validate a credential value and it does not contact GMGN. Do not make a live
request unless it reports `External GMGN API-key configuration detected: yes`.

## 4. Opt-in live contract check

Only perform this when no GMGN cooldown is active. Run one request, then stop and
inspect the result. Do not run it repeatedly.

```powershell
& ".\GmemMonitor\bin\x64\Debug\GmgnContractProbe.exe" `
  --node "C:\Program Files\nodejs\node.exe" `
  --cli-entry "E:\blockchain-projects\GMemMonitor\runtime\gmgn-cli\node_modules\gmgn-cli\dist\index.js" `
  --action follow-wallet
```

When the next intentionally scheduled live request is needed for development, capture
the response shape without ever saving the raw payload by adding the following option
to that same command (do not run a second request just to capture it):

```text
--sanitized-fixture "E:\blockchain-projects\GMemMonitor\tests\contract\fixtures\gmgn\follow_wallet_buy.json"
```

Run it from the repository root. The output file must be new. The probe preserves JSON
object keys and value types, but replaces every value before writing the file. Review
the fixture for user-controlled JSON keys before committing it.

The probe internally invokes the documented BSC BUY feed contract:

```text
node.exe <cli-entry> track follow-wallet --chain bsc --side buy --limit 100 --raw
```

The probe does not show raw output. It reports process state, exit code, bounded byte
counts, JSON syntax on success, and a safe rate-limit notice when the CLI reports 429.

For a one-record diagnostic run that shows normal CLI errors locally, use the CLI
directly **only instead of the probe**, not after it. It is a separate live
`follow-wallet` request and must follow the same one-request-and-stop rule. Its output
can contain account-sensitive activity, so do not paste it into issues or chat:

```powershell
Push-Location ".\runtime\gmgn-cli"

& "C:\Program Files\nodejs\node.exe" `
  ".\node_modules\gmgn-cli\dist\index.js" `
  track follow-wallet `
  --chain bsc `
  --side buy `
  --limit 1 `
  --raw

$LASTEXITCODE
Pop-Location
```

## 5. Rate-limit behavior

`HTTP 429`, `RATE_LIMIT_EXCEEDED`, or a message such as `IP rate limit exceeded`
means GMGN has rate-limited the public IP. It is not a project setting and cannot be
increased or bypassed by GMemMonitor.

- Stop all probe, CLI, and application GMGN requests immediately.
- Wait until GMGN's reported reset time, plus 30–60 seconds.
- Make one request only after the wait. Repeated requests can extend the cooldown.
- Choose either the probe or the direct CLI diagnostic command for that one request;
  never run both during the same validation attempt.
- If the cooldown persists after a quiet period, test from another IPv4 public
  network or contact GMGN through the channel linked in its rate-limit guidance.

The current pinned CLI has been observed to assert and exit with `0xC0000409` after a
429 on Windows. Treat the safe 429 classification as authoritative; it is a
CLI/runtime error-handling failure after GMGN's response, not proof that the API key
or private key is malformed.

The core `GmgnCliClient` recognizes this stderr pattern and blocks further requests
from that client for the reported remaining duration plus a small safety margin. When
GMGN does not report a duration, it uses a conservative 60-second local cooldown.
This protects application-controlled calls only; manual CLI commands and other tools
on the same public IP are outside its control.

## 6. Current acceptance boundary

A successful live probe verifies only the CLI contract and safe process invocation.
It does not yet demonstrate end-to-end dashboard monitoring, alert delivery,
pagination behavior, or portable-release readiness. Record only sanitized facts in
`Documentation/gmgn-cli-contract.md`; never save raw feed responses, wallet lists,
transaction data, credentials, signatures, or logs in the repository.
