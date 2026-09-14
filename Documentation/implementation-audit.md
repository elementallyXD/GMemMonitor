# Implementation Audit — 2026-09-07

## Outcome

GMemMonitor is **not yet an end-to-end monitor**. The repository contains useful
building blocks and a WinUI dashboard shell, but the live GMGN feed has not produced a
sanitized successful response fixture. The dashboard monitoring button is disabled; it
does not create a Node process, invoke `gmgn-cli`, or contact GMGN.

As of 2026-09-08, GMGN support has stated that `GET /v1/trade/follow_wallet` is
temporarily unavailable on the Free plan and requires Plus-plan access. This prevents
the required Phase 01 followed-wallet contract capture on a Free-plan account; it is a
product-access blocker rather than an application retry or IP-ban defect.

This proves that the dashboard did not cause the observed GMGN IP ban. The available
evidence cannot identify which manual request or other activity sharing the public IP
caused it. A new API key would not remove an IP-based ban and is not the next step.

## Evidence captured

| Check | Result | Meaning |
|---|---|---|
| `GmgnContractProbe --self-test` | Passed | Offline probe argument/JSON/rate-limit checks pass. |
| `GMemMonitor.MonitoringTests` | Passed | Existing offline aggregation and client-cooldown tests pass. |
| `GmgnContractProbe --action version` | Exit `0` | Node and the pinned local CLI entry point can launch. |
| `follow-wallet` live call | HTTP `429`, then CLI exit `0xC0000409` | GMGN rate-limited the public IP; the pinned CLI then asserted while handling the error. |
| GUI Start button | No GMGN work | The monitoring controller has not been implemented/wired. |

The core client recognizes bounded `HTTP 429`/`RATE_LIMIT_*` diagnostics and suppresses
further requests from the same client instance. Separate probe executions and direct
Node commands are separate processes, so they cannot share that in-memory cooldown.

## Phase-by-phase review

| Phase | Required outcome | Actual state | Next action |
|---:|---|---|---|
| 01 | Successful, sanitized live GMGN contract capture | Blocked by active IP ban; no success fixture, schema, ordering, or pagination evidence | Leave GMGN quiet for at least five minutes after the last request, then make one probe request only. Capture no raw data. |
| 02 | Typed GMGN parser, configuration, logs, process tests | Process runner and fixed command mapping exist; parser, app settings store, logging, and full process-fixture coverage are missing | Implement against a sanitized successful fixture after Phase 01 succeeds. |
| 03 | Controller, poll worker, baseline, retry, rate scheduling | Dedupe/aggregation primitives exist; no controller or poll worker | Implement only after the parser can produce typed records. |
| 04 | Enrichment service and native alert pipeline | Alert formatting, URL validation, and cooldown primitives exist; no live enrichment parser/service | Implement after token-info/security fixtures exist. |
| 05 | GUI dispatch, tray, lifecycle, settings persistence, notifications | Dashboard shell and inline validation exist; controller dispatch, persistence, tray, notifications, lifecycle are missing | Wire only to completed Phase 03/04 services; do not put GMGN logic in XAML code-behind. |
| 06 | Portable release/clean-VM validation | Partial staging artifacts only | Defer until Phases 01–05 pass. |

## Required implementation sequence

1. **Repair the external CLI configuration.** The current non-network
   `gmgn-cli config --check` preflight exits `1`, so the pinned CLI cannot detect
   `GMGN_API_KEY` for the account running the probe. Correct the user-owned
   `%USERPROFILE%\.config\gmgn\.env` according to GMGN's CLI instructions; do not
   copy its contents into this repository, logs, or chat. Confirm the preflight exits
   `0` before any feed request.
2. **End the current GMGN cooldown.** Do not run the probe, direct CLI, or any other
   GMGN client from the same public IP until GMGN's reported reset has elapsed, plus a
   30–60 second margin. Repeated calls can extend a ban.
3. **Perform one live probe call.** Run `follow-wallet` once through
   `GmgnContractProbe`; do not precede it with the direct CLI command. If it returns
   429 again after the quiet period, use another IPv4 public network or GMGN support.
4. **Capture a sanitized success fixture.** Preserve field names and value types while
   replacing addresses, IDs, links, labels, and transaction values. Never store the
   raw response.
5. **Implement and test the typed feed parser.** Confirm record ID, timestamp,
   amount-USD, maker, token, BSC/BUY fields, ordering, and `next_page_token` behavior.
6. **Implement the controller and scheduler.** It must have one polling worker, no
   overlap, one request at a time, initial baseline behavior, cancellation, and a
   shared rate-limit cooldown. The GUI can then start and stop this controller.
7. **Implement enrichment from fixtures, then notifications.** Keep every GMGN call
   behind the same scheduler and show factual risk information only.
8. **Wire the completed services to the GUI.** Persist non-secret settings, disable
   edits during the session, marshal status to the UI thread, and add tray/lifecycle
   behavior.

## Rate-limit facts and policy

- GMGN documents API/IP rate guidance and an escalation path in its
  [IP rate-limit guidance](https://docs.gmgn.ai/index/cooperation-api-data-crawling-ip-whitelist).
- GMGN's [Agent API guide](https://docs.gmgn.ai/index/gmgn-agent-api) states that
  requests must use IPv4; it does not provide a client-side switch to remove a ban.
- Changing the API key does not change the public IP that GMGN reported as banned.
- Do not lower the dashboard poll interval below the product default of 20 seconds.
  A single monitor process must never overlap feed, token-info, or security calls.
- The application must treat any HTTP 429 as a visible cooldown state and must not
  probe repeatedly to see whether the ban has ended.

## Explicit non-workarounds

- Do not add a second API key to evade a rate limit.
- Do not retry immediately, spawn concurrent CLI processes, or add undocumented CLI
  pagination flags.
- Do not send API keys/private keys to the application, source tree, logs, or chat.
- Do not wire raw/unverified GMGN JSON directly to the dashboard or aggregation code.
