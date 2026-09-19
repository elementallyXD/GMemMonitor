# GMGN CLI contract

This file owns the application's GMGN boundary and sanitized observations. Source and
fixtures establish local behavior; public provider documentation can change independently
of the pinned CLI. No authenticated request was made during the 2026-09-19 documentation review.

## Pins and operations

The tested pair is Node 24.12.0 / gmgn-cli 1.5.7. Exact hashes and paths are authoritative
in [runtime-manifest.json](../packaging/runtime-manifest.json) and the
[npm lockfile](../packaging/gmgn-cli/package-lock.json).

[GmgnCliClient](../src/core/src/GmgnClient.cpp) invokes bundled Node with the CLI entry
followed by these fixed arguments:

~~~text
track follow-wallet --chain bsc --side buy --limit 100 --raw
token info --chain bsc --address <normalized-token> --raw
token security --chain bsc --address <normalized-token> --raw
~~~

The feed uses API-key plus request-signing authentication from the external CLI configuration;
see [setup](operations.md). It uses current account follows, with no local enumeration or
frozen list. The USD filter is local: the app does not pass `--min-amount-usd`.

## Captured shapes

On 2026-09-14, the previous project record reports successful feed/info/security probes
(exit 0, valid JSON, no stderr) and sanitized captures. The checked-in fixtures preserve
structure/types while replacing **every value**:

| Fixture | Fields consumed by the application |
|---|---|
| [follow_wallet_buy.json](../tests/contract/fixtures/gmgn/follow_wallet_buy.json) | Object with `list` and `next_page_token`; records use `id`, `chain`, `side`, `maker`, `base_address`, `transaction_hash`, `amount_usd`, `base_amount`, `price_usd`, `timestamp`, optional `base_token.symbol` |
| [token_info_bsc.json](../tests/contract/fixtures/gmgn/token_info_bsc.json) | `address`, `symbol`, optional `locked_ratio` and `link.gmgn` |
| [token_security_bsc.json](../tests/contract/fixtures/gmgn/token_security_bsc.json) | `address`, optional boolean `is_honeypot`/`is_open_source`/`is_renounced`, optional tax fields |

The [parser](../src/core/src/GmgnJsonParser.cpp) accepts supported string/number
representations, normalizes strict addresses and fixed-point USD, and skips invalid feed
records with a rejection count. Optional absent/null risk facts stay unavailable.
Syntax/root failures reject the response. The fixtures cannot prove ordering, stable IDs,
real values/units, account membership, or live monitoring coverage.

## Provider limitations and drift

- The response includes `next_page_token`, but no supported feed cursor argument is
  verified for 1.5.7. The app requests only the latest 100 and does not claim lossless
  coverage. Overlapping-poll ordering and ID stability still need observations.
- On 2026-09-08, project notes recorded GMGN support reporting temporary Free-plan
  restrictions and Plus access for this endpoint. Success was recorded after a subscription
  change on September 14. Neither establishes current entitlements.
- **Mismatch found 2026-09-19:** the current official
  [tracking reference](https://github.com/GMGNAI/gmgn-skills/blob/main/skills/gmgn-track/SKILL.md)
  lists feed weight **10**, with Free 5/5, Plus 20/20, Pro 50/50 rate/capacity.
  The code still uses feed weight **3**, enrichment weight 1, and a fixed 20/20 bucket.
  Reconcile this before release; pinning the client does not pin server policy.
- The [CLI reference](https://github.com/GMGNAI/gmgn-skills/blob/main/docs/cli-usage.md)
  still shows signed dynamic follows and limit 1–100 without a feed cursor option.
  Its moving main branch is a reference, not a versioned contract fixture.
- [GMGN Agent API](https://docs.gmgn.ai/index/gmgn-agent-api) documents IPv4-only access.
  The application does not alter networking or bypass provider restrictions.

A historical September 7 observation received HTTP 429 / RATE_LIMIT_EXCEEDED, followed
by Node's `UV_HANDLE_CLOSING` assertion and Windows exit `0xC0000409`. That sequence
was a post-rate-limit CLI/runtime failure, not evidence of malformed credentials.

The current application recognizes HTTP 429 in stderr or RATE_LIMIT_EXCEEDED in captured
output. It extracts `Ns remaining` from stderr, adds two seconds, and caps the result
at 300 seconds; without a parsed duration it waits 62 seconds. It does not parse a reset
timestamp or every provider ban shape. Manual checks must respect the provider's full
reset time, which can exceed the application's wait. Generic nonzero exits also remain
distinct from typed transient errors; see [required fixes](roadmap.md).

## Probe and fixtures

Build Debug using [development](development.md). From the repository root, use the
bundled runtime and absolute paths:

~~~powershell
$probe = (Resolve-Path ./GmemMonitor/bin/x64/Debug/GmgnContractProbe.exe).Path
$node = (Resolve-Path ./runtime/node.exe).Path
$cli = (Resolve-Path ./runtime/gmgn-cli/node_modules/gmgn-cli/dist/index.js).Path
& $probe --node $node --cli-entry $cli --action version
& $probe --node $node --cli-entry $cli --action config-check
~~~

Both actions are non-network. Version checks CLI launch only. Config-check reports API-key
presence, not validity, signing-key correctness, or feed entitlement. Require a positive
preflight before opting into a live check.

Stop monitoring and other probes first; do not request during a provider cooldown.
For one intentional live feed check, run **one** of these alternatives:

~~~powershell
# Without saving a fixture:
& $probe --node $node --cli-entry $cli --action follow-wallet

# OR: use this instead of the command above when capturing a new shape.
$capture = Join-Path (Resolve-Path ./tests/contract/fixtures/gmgn).Path 'follow_wallet_review.json'
& $probe --node $node --cli-entry $cli --action follow-wallet --sanitized-fixture $capture
~~~

The output file must be new and directly inside the fixture directory. The probe replaces
values in memory before writing; object keys are retained and require manual privacy review.
Never redirect raw CLI output to disk. Capture token shapes using `--action token-info`
or `--action token-security` with `--token <public-bsc-address>`, each a separately
authorized request. Preserve command, versions, date, sanitized category and shape;
update parser tests when an observed contract changes.

The probe prints safe process/byte/JSON summaries, not records or stable-ID overlap counts.
The proposed overlap tool in the roadmap is needed for reproducible private comparisons.

The optional [Test-LiveGmgn.ps1](../scripts/Test-LiveGmgn.ps1) wrapper requires
`-EnableLiveGmgn`. It makes one feed request; supplying `-Token` adds info and security
requests separated by two seconds. A missing configuration prints “skipped” and exits 0:
that is **not** a live-test pass. Stop on any failure; do not chain further diagnostics.
