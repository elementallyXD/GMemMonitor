# GMGN Capability Roadmap

## Purpose

This document distinguishes documented GMGN capabilities from approved product scope.
Availability, route weights, plans, supported chains, and response contracts can change;
each capability must be revalidated against the pinned CLI before implementation.

## Capability inventory

| Capability | Value | MVP status |
|---|---|---|
| Followed-wallet trade activity | Detect coordinated activity from wallets the user follows in GMGN. | Required after Plus access. |
| KOL and Smart Money feeds | Add independent public-wallet context alongside personal follows. | Future research feature. |
| Token, market, and security data | Show liquidity, taxes, honeypot/security facts, holders, traders, and market context. | Token/security enrichment is MVP; broader data is future. |
| GMGN market signals | Surface factual events such as large/multiple buys, KOL or Smart Money buys, platform calls, price spikes, and social/marketing events. | Future contextual alerts only. |
| Social/X fields | Token social links, X follower counts, and some wallet/KOL usernames. | Future context only. Not an arbitrary X-post tracking API. |
| Swap and strategy orders | Market swaps, limit orders, take-profit, stop-loss, and trailing strategies on GMGN-documented execution chains. | Explicitly out of scope. |

## Useful future research ideas

These are decision-support hypotheses, not trading instructions and not profit promises.

1. **Convergence review.** Show a token when several independently followed wallets
   buy it within a short window, together with time, amount, and full/partial-position
   facts where GMGN supplies them.
2. **Signal corroboration.** Add GMGN-provided KOL/Smart Money or multiple-buy signals
   as labelled context. Never treat a provider signal as confirmation that a token is
   legitimate or likely to rise.
3. **Risk-first review.** Put honeypot, tax, liquidity, ownership, holder-concentration,
   and bundled-wallet facts beside the alert so a person can reject risky tokens quickly.
4. **Wallet-quality research.** Evaluate candidate followed wallets over a documented
   historical observation period before placing weight on their activity. This requires
   a separate data-retention, methodology, and privacy decision; it is not MVP scope.
5. **Social context.** Display only provider-returned social links/follower facts next to
   a token. A genuine X-post tracker would require a separate approved provider, terms,
   rate limits, and abuse/privacy review.

## Future execution boundary

GMGN documents swaps and conditional strategies, but execution is a different product
from monitoring. A future execution product would need explicit user authorization or
time-bounded one-shot arming, a dedicated security threat model, bounded loss/risk
controls, order-state reconciliation, audit records, and its own clean-VM validation.
It must not reuse this MVP's read-only process or accept blockchain wallet keys without
a new, explicit product decision.

The scoped feasibility decision for an explicitly armed, unattended one-shot Base
purchase is recorded in the
[Base launch executor plan](../Future/base-launch-executor-plan.md). It remains a
separate product and does not change this application's read-only MVP.

## Financial-risk statement

Memecoin feeds, wallet clusters, KOL calls, social signals, and take-profit/stop-loss
orders do not establish expected profitability. Prices can move before a feed arrives;
liquidity can vanish; spreads, slippage, MEV, manipulation, provider outages, and failed
orders can create losses. The product may support manual research, but it must not claim
to predict returns or automatically act on signals.
