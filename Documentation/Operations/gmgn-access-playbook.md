# GMGN Access Playbook

## Scope

Use this procedure only for the pinned Phase 01 CLI contract probe. It never authorizes
credential sharing, unsupported flags, retry loops, or attempts to bypass GMGN limits.

## Current plan limitation

GMGN support reported on 2026-09-08 that `GET /v1/trade/follow_wallet` requires
Plus-plan access while temporarily limited on Free. Do not run repeated probes on Free;
they cannot establish the required success contract.

## After Plus access

1. Keep the user-owned credentials only in `%USERPROFILE%\.config\gmgn\.env`.
2. Run the probe's non-network `config-check`; it must report that the API key is
   detected. It does not validate or disclose the key.
3. If any prior request returned 429, stop every GMGN request and wait at least five
   minutes plus a safety margin from the latest API 429.
4. Submit exactly one `follow-wallet` probe request. Do not follow it with a direct CLI
   command.
5. If it succeeds, create a sanitized fixture preserving JSON structure and types while
   replacing account-identifying values. Do not store raw output.
6. If it returns 429, stop. Report only the safe probe summary, timestamp, public-IP
   details through GMGN's private support channel if requested, and API-key label/suffix
   if support asks. Never disclose a key or private key.

## Rate-limit behavior

Treat API HTTP 429 as a provider-enforced stop condition. The CLI can itself retry once
for a short cooldown; application code must still centralize admission and must not
schedule additional requests during a reported ban. A new API key does not clear an
IP-based provider ban or a plan restriction.
