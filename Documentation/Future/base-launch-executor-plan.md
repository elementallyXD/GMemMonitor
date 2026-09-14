# Base Launch Executor Feasibility and Implementation Plan

Status: proposed separate product; live execution is not implemented or approved  
Last evidence review: 2026-09-09  
Target network: Base mainnet, chain ID `8453`

## Decision

Do not add transaction signing or unattended trading to GMemMonitor. Its approved
security boundary is read-only, BSC-only, and contains no blockchain wallet key. Build
any launch executor as a separate repository, process, configuration, and release.

The quickest defensible first implementation is a hybrid:

1. Observe Base directly through a WebSocket/Flashblocks provider for low-latency pool
   and liquidity events.
2. Use GMGN Plus as the first execution adapter because the official launch venue is
   not yet stable and GMGN abstracts route construction and submission.
3. Add a direct DEX adapter only after the actual venue, router, pool type, and
   chain-specific addresses are known and verified.

Direct RPC execution has the lowest theoretical latency when the exact route is known,
but it is materially harder to implement safely in C++ and requires custody of a real
hot-wallet signing key. GMGN is easier to deliver and avoids putting that blockchain
key in the application, but adds provider latency, rate/plan dependency, and has no
documented guarantee of first-block inclusion. Neither approach can guarantee that the
purchase is first or profitable.

## Evidence snapshot

The target supplied by the user is:

```text
0xB095274743941e953c746F9C228DA9c18Bb6ec29
```

As of the evidence review:

- The project [website](https://www.laptoptoken.com/) and
  [FAQ](https://www.laptoptoken.com/faq) publish that exact Base address. The FAQ says
  Hunter Biden is the founder and names Fomo and Pump as purchase routes, while the
  homepage still says `Buy Now Coming soon`. The UI is therefore not reliable evidence
  that a venue is live.
- A read-only Base RPC call returned deployed bytecode. Sourcify reports an exact
  creation/runtime source match for the address, Solidity 0.8.22, and no proxy:
  [verified source](https://repo.sourcify.dev/8453/0xB095274743941e953c746F9C228DA9c18Bb6ec29/).
- The verified `LaptopOFT` source is an OpenZeppelin ERC-20 plus LayerZero OFT and
  `Ownable2Step`. It has 18 decimals and a one-billion-token initial Base supply.
- The verified source has no `tradingEnabled` switch, transfer tax, pause, blacklist,
  freeze, cooldown, maximum-wallet rule, or direct owner-mint function. A generic
  "trading enabled" trigger cannot be implemented for this contract.
- The owner is nonzero and is currently a 2-of-3 Safe. The owner cannot directly mint,
  but retains LayerZero peer, delegate, option, inspector, and pre-crime configuration
  powers. No timelock is enforced by the token contract.
- The site-hosted [Hacken audit](https://www.laptoptoken.com/token-audit.pdf) reports no
  unresolved critical/high/medium findings for its reviewed commit, but calls out
  centralized administration, missing timelocks, and bridge/DVN configuration risks.
  An audit is not proof that a deployment or market is safe.
- At the review snapshot, no indexed GeckoTerminal pool contained the exact address.
  Search results did show active `LAPTOP` pools whose token *name* was set to the
  official contract address while the actual token address differed. For example, one
  indexed pool displays `0xb20...7f01`, not the target address:
  [copycat example](https://www.geckoterminal.com/base/pools/0xd0d6504fe8bccfc98a190068942b3fba332ebd7ebff7f6273882503308b73109).

Market availability is time-sensitive. The application must obtain fresh on-chain
evidence and must never trust a symbol, name, search-result title, social post, or
third-party pool label as an address match.

## Correct launch trigger

The fast trigger is not a single event. Every gate below must pass in order:

1. The application is explicitly armed for Base chain ID `8453`, the exact target
   address, an expiry time, and one approved spending policy.
2. A pool involving the exact 20-byte target and canonical WETH is discovered from an
   allowlisted, code-verified DEX factory or pool manager.
3. The factory/registry confirms that the pool is canonical for its token pair and pool
   parameters. Token ordering, fee tier, hooks, and router are validated.
4. The pool is initialized and has at least the configured usable WETH-side liquidity.
   Pool creation or initialization alone is insufficient.
5. A fresh exact-input quote for `0.010 ETH` succeeds against Base `pending` state and
   is below the configured maximum entry price or FDV.
6. The exact proposed transaction succeeds in `eth_call`/gas estimation or
   `eth_simulateV1` against the same fresh pending state.
7. Tax, sellability, code, ownership, price impact, quote age, fee, nonce, balance, and
   total-budget checks pass. Unknown values fail closed.
8. A durable one-shot latch is written before signing/submission. Only one nonce and one
   buy attempt are allowed for the armed session.

An optional safer-but-later mode may wait for a meaningful first successful public
swap, then re-quote and re-simulate. A dust or self-swap is not sufficient. Watching a
`tradingEnabled` function is not available for this contract, and firing on
`PoolCreated` alone is unsafe because anyone can create an empty or manipulated pool.

Base Flashblocks expose pending state and preconfirmed updates at roughly 200 ms. Use
subscriptions plus sealed-log backfill after reconnects; do not aggressively poll.
See the official [Base RPC overview](https://docs.base.org/base-chain/api-reference/rpc-overview).

## Spending and risk policy

The user's `0.025 ETH` limit must be represented in wei and enforced as an invariant,
not as a UI estimate:

```text
buy value                    = 0.010 ETH
maximum session debit        = 0.025 ETH
entry fee ceiling            = configurable within the remaining budget
reserved ETH for an exit     = mandatory
buy value + worst-case Base fees + reserve <= 0.025 ETH
```

Base fees contain L2 execution and L1 security components. Estimate both before
signing; a simple `gasLimit * maxFeePerGas` calculation is incomplete. See
[Base network fees](https://docs.base.org/specifications/transactions/network-fees).

The following corrections are mandatory:

- Slippage is not a fee reserve. It is permission to accept a worse execution price.
- `amountOutMinimum` may never be zero. Compute it from a fresh quote with integer
  rounding and a configured `maxSlippageBps`; abort if the result is zero.
- `0.01 ETH` is not a valid token-tax setting. On a `0.01 ETH` purchase it could permit
  a 100% loss. Buy and sell taxes must be expressed independently in basis points.
- Because the verified token has no tax logic, an unknown or material simulated tax is
  anomalous and must abort the attempt.
- Slippage and tax caps do not replace an absolute maximum token price or FDV. That
  value and a minimum WETH liquidity threshold are required before unattended mode can
  be enabled.

Provisional engineering limits, subject to explicit operator approval before coding,
are a 10% slippage default, an unoverrideable 20% hard cap, no more than 1% observed
buy or sell transfer loss, a short transaction deadline, and one attempt. Failure to
buy within those limits is safer than silently spending the budget for almost no
tokens.

## Unattended operating model

The user requested no confirmation at execution time. Implement that as explicit
time-bounded arming, not as permanent auto-trading:

- The GUI always starts disarmed and remains disarmed after crash, update, sleep, or
  reboot.
- Before arming, it displays the full address, chain, budget, fee ceiling, liquidity
  floor, price/FDV ceiling, slippage, tax limits, route policy, and arm expiry.
- After the user presses **Arm**, the application may submit one qualifying trade
  without another prompt until the arm window expires.
- **Disarm/Kill** cancels observation and all work that has not been signed.
- A rejected or reverted transaction is terminal. If transport acknowledgement is
  ambiguous, only identical signed bytes may be resent; never sign a second nonce.
- The GUI separately reports `buy confirmed`, `buy reverted`, `submission unknown`,
  and `strategy protection missing`.

GMGN's attached take-profit/stop-loss creation is best effort: a purchase can succeed
while strategy creation fails. The executor must verify every returned strategy order
ID and raise a critical notification if protection was not installed. TP/SL also does
not guarantee an exit during illiquidity or a gap.

## Security design

- Use a dedicated hot wallet funded only with the explicitly approved exposure. Never
  use a primary wallet or seed phrase.
- In the first GMGN adapter, continue loading API credentials from GMGN's external
  configuration. Never copy secrets into project files, logs, crash dumps, or UI.
- A future direct adapter must isolate signing in a small process and protect the key
  with Windows user-scoped OS storage and strict file ACLs. A hardware wallet is safer
  but normally incompatible with unattended signing.
- Pin and verify the target address, Base chain ID, target code hash, canonical WETH,
  factory, router, and bundled dependency hashes before arming.
- Compare addresses as 20-byte values. Never compare token names or symbols.
- Bound queues, reconnect attempts, captured provider output, and diagnostic retention.
- Redact wallet balances, signed payloads, API credentials, and raw provider responses.
- Refuse to arm when two independent read providers disagree on chain head, code, pool,
  quote, nonce, or balance.
- Do not use a geographic/consumer proxy. It adds latency and another trust/failure
  boundary and does not improve Base sequencer ordering. Use a reputable low-latency
  WebSocket provider plus an independent read-only verifier instead.
- Do not rotate IPs or retry through proxies to evade GMGN or RPC limits.

## Proposed solution structure

```text
BaseLaunchExecutor/
  AGENTS.md
  README.md
  Documentation/
    architecture.md
    threat-model.md
    gmgn-contract.md
    operations.md
    release-checklist.md
  src/
    BaseLaunch.App/          WinUI 3 GUI and Windows notifications
    BaseLaunch.Core/         policy, state machine, fixed-point types
    BaseLaunch.BaseRpc/      subscriptions, backfill, quote/simulation reads
    BaseLaunch.Gmgn/         pinned CLI execution adapter
    BaseLaunch.Signer/       future direct signer; excluded from first release
  tests/
    BaseLaunch.Core.Tests/
    BaseLaunch.IntegrationTests/
    fixtures/
  scripts/
```

Keep XAML and view models free of chain parsing and trade policy. Suggested core
interfaces are `IChainObserver`, `IDexAdapter`, `IQuoteSimulator`, `IRiskPolicy`,
`IExecutionAdapter`, `IOneShotStore`, and `IClock`. The state machine is:

```text
Disarmed -> ArmedWaiting -> Candidate -> Validating -> Submitting
        -> Confirming -> Completed
                     \-> Aborted
```

Every transition and reason is written to a small redacted local audit record. The
one-shot latch is persisted before `Submitting`, so a crash cannot trigger another buy.

## Delivery phases and gates

### Phase 0: freeze the execution contract

- Record the exact CA/source/code hash and current owner/admin facts.
- Obtain fresh evidence of the actual launch venue and exact-address pool.
- Pin the tested Node, `gmgn-cli`, Base RPC behavior, and GMGN Plus limits.
- Decide maximum entry price/FDV, minimum WETH liquidity, entry-fee ceiling, exit
  reserve, slippage basis points, and buy/sell tax basis points.

Exit: every risk parameter and external route is explicit; no guessed router remains.

### Phase 1: separate GUI and deterministic core

- Create the C++20 WinUI 3 solution and projects above.
- Implement address/wei/basis-point types, immutable armed settings, state machine,
  one-shot journal, cancellation, bounded logging, and notifications.
- Port only the generic, reviewed process-runner pattern from GMemMonitor.

Exit: offline unit and crash-recovery tests pass; no network or signer is present.

### Phase 2: observation-only Base integration

- Add Flashblocks/WebSocket observation, reconnect with sealed-log backfill, exact
  address matching, allowlisted factory adapters, liquidity checks, and quote/simulation.
- Run with `would_trade` output only. No API trade permission and no signer.

Exit: multi-day shadow mode shows no duplicate, spoofed, stale, or missed reconnect
events in the captured test scenarios.

### Phase 3: GMGN Plus execution adapter

- Invoke only a pinned bundled CLI through a no-shell process runner.
- Require explicit armed mode, one attempt, a nonzero minimum output, bounded EIP-1559
  fees, and immediate stop on 429/auth/provider failure.
- Reconcile the swap receipt separately from any attached TP/SL order IDs.

Exit: sanitized Plus-plan fixtures, Base Sepolia/mocked execution, timeout,
cancellation, ambiguity, and rate-limit tests pass.

### Phase 4: adversarial validation

- Test token ordering, fake factories/topics, dust and out-of-range liquidity, removed
  logs, reorgs, duplicates, stale quotes, owner/code changes, and RPC disagreement.
- Use adversarial tokens for transfer loss, sell revert/honeypot, mutable blacklist,
  maximum-wallet, proxy upgrade, and fee-on-transfer behavior.
- Replay historical Base states and run chaos tests for disconnect, crash after signing,
  nonce conflict, duplicate submission, and ambiguous acknowledgement.

Exit: all fail-closed and budget-boundary tests pass on a clean Windows 11 x64 VM.

### Phase 5: controlled mainnet arming

- Fund only the dedicated capped wallet.
- Validate the final official venue and exact-address pool from two independent sources.
- Review a fresh shadow-mode report, then enable one time-bounded armed session.

Exit: one terminal outcome is recorded and the application automatically disarms.

### Phase 6: optional direct DEX adapter

Implement only after the official venue is known. Add router-specific ABI encoding,
local signing, nonce and EIP-1559 handling, Base L1 fee estimation, simulation, exact
submission, and receipt reconciliation. Repeat the full threat-model and clean-VM gate.

## Mainnet blockers

Live execution must remain disabled until all of these are resolved:

1. An exact-address pool and official/canonical route are freshly verified.
2. A maximum entry price or FDV and a minimum WETH liquidity floor are selected.
3. Slippage and buy/sell tax limits are approved in basis points.
4. The `0.025 ETH` budget is split between entry fee ceiling and mandatory exit reserve.
5. GMGN Plus swap behavior, limits, and automated-trade configuration are captured with
   sanitized fixtures, or a direct DEX adapter passes equivalent tests.
6. Jurisdiction, provider terms, and venue eligibility are reviewed. No proxy is used
   to bypass a restriction.

This plan is an engineering risk-control document, not investment advice or a claim of
expected profit. A correct transaction can still buy at a poor price, be sandwiched,
lose liquidity, fail to sell, or lose the full funded amount.
