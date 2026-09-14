# GMGN CLI Contract — Phase 01

## Status

The package metadata is pinned. An earlier live BSC `follow-wallet` attempt from the
documented development environment reached GMGN and received an IP rate-limit response
(HTTP 429). On 2026-09-14, after a subscription change, a subsequent single probe
completed with exit code `0`, syntactically valid JSON stdout, and no stderr. The probe
reported 146,210 stdout bytes without printing or saving raw account data.

This verifies successful authentication and the process contract. A sanitized
`follow_wallet_buy.json` fixture was captured on 2026-09-14. It verifies that the root
is a JSON object containing a `list` array and a string `next_page_token`. Feed records
contain string identifiers/addresses and numeric representations for USD values,
prices, token quantities, and timestamps. The fixture deliberately replaces every
response value, so it does not establish record ordering or ID stability.

On the same date, `token-info` and `token-security` probes for a public BSC token each
completed with exit code `0`, valid JSON stdout, and no stderr. Their sanitized
fixtures are `token_info_bsc.json` and `token_security_bsc.json`. Token-info contains
an address, symbol, numeric `locked_ratio`, and a `link` object whose `gmgn` member is
a string. Token-security contains explicit boolean `is_honeypot`, `is_open_source`,
and `is_renounced` members plus string tax fields. The fixtures establish only shape
and types: every response value has been replaced.

No public, supported CLI cursor option has been verified. The MVP therefore continues
to request the latest 100 records without inventing a page-token argument.

On 2026-09-07, the pinned CLI's non-network `config --check` command exited with `1`
in one development execution context. It did not detect an external GMGN API key there.
This check did not display, copy, or inspect any credential value. The successful
2026-09-14 account probes prove that the user's normal execution context can read the
external configuration; the earlier result must not be treated as a user configuration
failure.

## Support-reported Free-plan limitation — 2026-09-08

After a clean single-request test, GMGN support reported that trading-related endpoints,
including `GET /v1/trade/follow_wallet`, are temporarily limited on the Free plan. It
reported that ordinary read/query endpoints are unaffected and that Plus-plan access is
required for this endpoint at present. This is a time-sensitive support statement, not a
permanent protocol guarantee; revalidate it before release or after changing plans.

The endpoint is read-only for this application but is nevertheless plan-gated. Do not
attempt to work around the restriction with extra keys, retries, or IP changes. The MVP
cannot meet its dynamic followed-wallet monitoring requirement on the Free plan.

## Sources

- [GMGN Agent API](https://docs.gmgn.ai/index/gmgn-agent-api): API-key/key-pair setup and external `~/.config/gmgn/.env` convention.
- [Official CLI usage](https://github.com/GMGNAI/gmgn-skills/blob/main/docs/cli-usage.md): command syntax for followed-wallet monitoring.
- [Official tracking skill](https://github.com/GMGNAI/gmgn-skills/blob/main/skills/gmgn-track/SKILL.md): signed authentication, route weights, response fields, and rate-limit guidance.
- [GMGN IP rate-limit guidance](https://docs.gmgn.ai/index/cooperation-api-data-crawling-ip-whitelist): default request-frequency guidance and the support channel for suspected erroneous blocks.

For `track follow-wallet`, the CLI documentation is the specific source: it states that this operation requires `GMGN_API_KEY` and `GMGN_PRIVATE_KEY` for signed authentication. The generic Agent API page describes private-key use primarily in trading examples, so it must not be interpreted as overriding the command-specific requirement.

## Pinned metadata

| Item | Value |
|---|---|
| Node.js used for the development probe | `24.12.0` |
| CLI package | `gmgn-cli` |
| CLI version | `1.5.7` |
| Source repository | `GMGNAI/gmgn-skills` |
| npm integrity | `sha512-vxI0uDlhRL4Rn6tJV3z2VRPGnmKDneA6/WdcwlFvHyUMh6pCClLF4f8Srk1xpoJvb/DhxXk0K/b1cTSA98sNJQ==` |
| Package entry point | `dist/index.js` |

The corresponding machine-readable record is [runtime-manifest.json](../packaging/runtime-manifest.json). Package files are intentionally not checked in during this phase.

## Required command contract

The probe executes only this followed-wallet command:

```text
node.exe <pinned-cli-entry> track follow-wallet --chain bsc --side buy --limit 100 --raw
```

Documented properties:

| Property | Contract |
|---|---|
| Supported chain | `bsc` |
| Direction filter | `buy` |
| Limit | Integer from 1 to 100; MVP uses 100 |
| Authentication | API key plus signing private key |
| Route weight | 3 under the documented 20-token rate/capacity bucket |
| Output | Raw JSON on stdout when `--raw` is supplied |
| Response root | `list` plus optional `next_page_token` |
| Dedupe candidate | Record `id` |

The public CLI options list does not document a follow-wallet cursor/page-token argument, even though the response documents `next_page_token`. The MVP therefore treats pagination as unresolved until a live, officially supported behavior is confirmed. It must not pass undocumented options.

## Required live observations

Run the probe only after installing a pinned CLI copy under a controlled runtime folder and configuring the GMGN credentials outside the repository. Record only sanitized facts:

- CLI/Node versions and package integrity verification result.
- Authentication success/failure category.
- Root JSON shape, key types, list ordering, ID stability, and overlap behavior.
- Pagination support or lack of supported support.
- Exit code and whether stdout is valid JSON.
- Error category for timeout, 401/403, 429, and malformed output where safely observable.
- Token-info and token-security response shape for a sanitized BSC fixture.

Do not record raw responses, request headers, environment variables, wallet lists, keys, signatures, transaction hashes, or account identity.

## Observed rate-limit behavior — 2026-09-07

The following sanitized facts were observed using Node.js `24.12.0` and `gmgn-cli`
`1.5.7`:

- `--action version` completed successfully.
- `track follow-wallet --chain bsc --side buy --limit 1 --raw` reached GMGN and
  received `HTTP 429` / `RATE_LIMIT_EXCEEDED` for the public IP.
- GMGN included a reset time and remaining-cooldown text in stderr, and warned that
  repeated requests may extend the cooldown.
- After the CLI received that 429, the Node runtime asserted in `UV_HANDLE_CLOSING`
  and exited with Windows status `0xC0000409`. This is treated as a CLI/runtime
  failure after a GMGN rate-limit response, not as an authentication verdict.

The probe and core client inspect only bounded captured output to classify this error.
They do not display or persist the raw stderr. Further manual testing must wait for the
reported reset and make a single request; see [human testing](human-testing.md).

## Observed token-enrichment behavior — 2026-09-14

Using the same pinned Node.js and CLI versions, the following sanitized facts were
observed for one public BSC token contract:

- `token-info` exited `0`, produced syntactically valid JSON, wrote no stderr, and
  produced 4,833 bytes of stdout before sanitization.
- `token-security` exited `0`, produced syntactically valid JSON, wrote no stderr,
  and produced 762 bytes of stdout before sanitization.
- Both fixture captures replace every value, retain only JSON structure and types, and
  contain no raw token metadata, account data, or credentials.

## Development-environment note

The system Node executable is available, but its `npm` PowerShell shim is currently affected by a user-level npm prefix. Development commands should invoke the system npm CLI through Node with a workspace-local cache until that machine configuration is repaired:

```text
C:\Program Files\nodejs\node.exe C:\Program Files\nodejs\node_modules\npm\bin\npm-cli.js --cache <workspace>\.cache\npm ...
```

This workaround is for local development only. The shipped application will invoke its own bundled `node.exe` and pinned CLI entry point directly.
