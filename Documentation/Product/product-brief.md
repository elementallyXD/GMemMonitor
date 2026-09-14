# Product Brief

## Product

GMemMonitor is a personal-use, native Windows 11 x64 desktop application. It monitors
**BSC BUY activity** from wallets currently followed by the user's GMGN account,
detects five distinct-wallet purchases of the same token within a configured window,
adds factual GMGN token/security context, and sends a Windows notification for manual
review.

## MVP promise

- A WinUI 3 GUI starts and stops monitoring.
- Monitoring uses GMGN's dynamic followed-wallet feed; it never invents a frozen wallet
  list or displays a fabricated followed-wallet count.
- Default signal: five distinct followed wallets, each buying at least $100, within 60
  seconds.
- Alerts include facts returned by GMGN and an approved GMGN link for manual review.
- The application is BSC-only, read-only, and starts OFF.

## Explicit MVP exclusions

- No swaps, order submission, take-profit, stop-loss, wallet management, signing, or
  blockchain private keys.
- No automated purchase/sale decision, profit claim, safety score, or investment advice.
- No multi-chain abstraction, social-media tracking service, historical database,
  backend, local web server, analytics telemetry, or auto-update service.

## Current external blocker

On 2026-09-08, GMGN support stated that the required read-only
`GET /v1/trade/follow_wallet` endpoint is temporarily limited on the Free plan and
requires Plus-plan access. This prevents live contract capture and therefore prevents
the MVP from advancing beyond offline work. It is not an application retry, key, or
public-IP workaround problem.

After Plus access is available, the next gate is one successful, sanitized
`follow-wallet` fixture. Only then can parser, poller, and GUI-controller work proceed.
