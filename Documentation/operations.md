# Setup and troubleshooting

## GMGN configuration

Create a request-signing key pair locally using GMGN's
[key-generation guide](https://docs.gmgn.ai/index/generate-public-key), then upload only
the public key when creating an API key. Configure `GMGN_API_KEY` and
`GMGN_PRIVATE_KEY` in `%USERPROFILE%/.config/gmgn/.env`, outside both repository and
portable folder. The pinned CLI accepts a single-line private PEM with literal `\n`
separators. Keep complete PEM boundaries; never share the file or its values.

This key signs GMGN API requests, not blockchain transactions. The application does not
read/write the file or store credentials. Set up follows in the GMGN account bound to
the API key; they are resolved dynamically on each feed request.

GMGN documents IPv4-only API access. Plan access must be checked with GMGN; the old
Plus-only support statement is historical, not a current entitlement guarantee.
See [Agent API setup](https://docs.gmgn.ai/index/gmgn-agent-api) and the
[observed contract and limitations](gmgn-cli-contract.md).

## Local data

| Path under `%LOCALAPPDATA%/GMemMonitor` | Use |
|---|---|
| `config.json` | Validated, atomically replaced, non-secret settings |
| `logs/gmemmonitor.log` | Redacted event diagnostics; default 2 MiB rotation threshold and five files total |

Settings load defaults with a warning if invalid; loading does not overwrite the file.
Stop the application before inspecting or moving an invalid config aside. Save through
the dashboard to regenerate it. The schema uses integer `minimum_buy_usd_micros`, not a
floating-point or string dollar field; [Settings.cpp](../src/core/src/Settings.cpp)
owns its exact fields/ranges. There is no signal-score setting.

Stop monitoring before changing settings. Restarting a monitoring session clears the
baseline, clusters, analyses, and cooldowns. Portable-folder replacement preserves the
external settings and credentials. Never copy local data into a release artifact.

## Diagnose

| Symptom | Check |
|---|---|
| Bundled runtime is missing | Run the staged/extracted executable with its complete sibling `runtime` directory; a raw build output is insufficient |
| Notifications unavailable | Registration blocks Start. Check local HRESULT diagnostics and that `Microsoft.WindowsAppRuntime.Insights.Resource.dll` is present; validate on the clean VM |
| Authentication required | Correct Windows user/profile, matched public/private key pair, full one-line PEM, and endpoint entitlement; use the non-network probe preflight |
| Retrying / rate limited | Stop app/probe requests; wait for the provider's full reported reset plus a margin. The current app's capped delay may be too short |
| Monitoring but no alert | Only post-start, individually qualifying BUYs from distinct followed wallets count; consider cooldown, feed truncation, enrichment errors in logs, and Windows notification settings |
| Defaults loaded | Invalid, unsupported, or inaccessible config; inspect locally and save valid settings |
| UI says “just now” after waiting | The label does not age automatically; it is not a freshness guarantee |

Live diagnostics are opt-in: use the [probe procedure](gmgn-cli-contract.md#probe-and-fixtures).
Do not loop manual requests, run parallel probes, rotate identities/networks to evade
restrictions, or induce live failures. On 429, stop all GMGN clients under your control;
if access remains blocked after the reported quiet period, contact GMGN support.

For bug reports, include app/source version, Windows build, sanitized error category/HRESULT,
and reproduction steps. Do not attach credentials, raw CLI output, wallet lists, transaction
activity, or unreviewed local logs.
