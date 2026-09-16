#include "gmemmonitor/core/Alerts.h"
#include "gmemmonitor/core/Analysis.h"
#include "gmemmonitor/core/GmgnClient.h"
#include "gmemmonitor/core/Logging.h"
#include "gmemmonitor/core/Monitoring.h"
#include "gmemmonitor/core/MonitoringSession.h"
#include "gmemmonitor/core/Process.h"
#include "gmemmonitor/core/Settings.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stop_token>
#include <thread>
#include <windows.h>

using namespace gmemmonitor::core;

namespace {
[[nodiscard]] EvmAddress Address(const char suffix) {
    std::string text{"0x000000000000000000000000000000000000000"}; text.push_back(suffix);
    return *EvmAddress::Parse(text);
}
[[nodiscard]] EvmAddress Address(const unsigned char suffix) {
    EvmAddress address;
    address.bytes.back() = static_cast<std::byte>(suffix);
    return address;
}
[[nodiscard]] WalletBuyEvent Event(char token, char wallet, std::int64_t usdMicros, std::chrono::system_clock::time_point timestamp, std::string key) {
    return {std::move(key), {}, Address(wallet), Address(token), {}, {usdMicros}, {}, {}, timestamp};
}
void Expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
template <typename T>
const T& RequireValue(const std::optional<T>& value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
    return *value;
}
template <typename T, typename... Alternatives>
const T& RequireAlternative(const std::variant<Alternatives...>& value, const char* message) {
    const auto* result = std::get_if<T>(&value);
    if (!result) { std::cerr << message << '\n'; std::exit(1); }
    return *result;
}
template <typename Predicate>
void ExpectEventually(Predicate predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (!predicate() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    Expect(predicate(), message);
}

class FakeProcessRunner final : public IProcessRunner {
public:
    ProcessResult result;
    std::size_t callCount{};

    ProcessResult Run(const ProcessRequest&, std::stop_token) override {
        ++callCount;
        return result;
    }
};

class FakeClock final : public IClock {
public:
    std::chrono::system_clock::time_point now{};
    [[nodiscard]] std::chrono::system_clock::time_point UtcNow() const override { return now; }
};

class FakeAlertClock final : public IAlertClock {
public:
    std::chrono::steady_clock::time_point now{};
    [[nodiscard]] std::chrono::steady_clock::time_point SteadyNow() const override { return now; }
};

class FakeNotificationService final : public INotificationService {
public:
    bool accept{true};
    std::vector<TokenAlert> alerts;
    bool Show(const TokenAlert& alert) override { if (accept) alerts.push_back(alert); return accept; }
};

class FakeEnrichmentClient final : public IGmgnClient {
public:
    TokenInfo info;
    GmgnResult<TokenSecurity> security;
    std::deque<GmgnResult<TokenSecurity>> securityResults;
    std::size_t infoCalls{};
    std::size_t securityCalls{};
    GmgnResult<FollowWalletPage> FetchFollowWalletBuys(std::stop_token) override { return FollowWalletPage{}; }
    GmgnResult<TokenInfo> FetchTokenInfo(const EvmAddress&, std::stop_token) override { ++infoCalls; return info; }
    GmgnResult<TokenSecurity> FetchTokenSecurity(const EvmAddress&, std::stop_token) override {
        ++securityCalls;
        if (securityResults.empty()) return security;
        auto next = std::move(securityResults.front());
        securityResults.pop_front();
        return next;
    }
};

class AuthenticationDuringAnalysisClient final : public IGmgnClient {
public:
    explicit AuthenticationDuringAnalysisClient(FollowWalletPage initial) : initial_(std::move(initial)) {}
    GmgnResult<FollowWalletPage> FetchFollowWalletBuys(std::stop_token) override { return initial_; }
    GmgnResult<TokenInfo> FetchTokenInfo(const EvmAddress&, std::stop_token) override {
        return GmgnFailure{GmgnFailureCode::Authentication, "authentication rejected"};
    }
    GmgnResult<TokenSecurity> FetchTokenSecurity(const EvmAddress&, std::stop_token) override {
        return GmgnFailure{GmgnFailureCode::Authentication, "authentication rejected"};
    }
private:
    FollowWalletPage initial_;
};

[[nodiscard]] std::filesystem::path ExistingSourceFile() {
    return std::filesystem::absolute(std::filesystem::path(__FILE__));
}

[[nodiscard]] std::string ReadFollowWalletFixture() {
    const auto fixture = ExistingSourceFile().parent_path().parent_path().parent_path() /
        "tests" / "contract" / "fixtures" / "gmgn" / "follow_wallet_buy.json";
    std::ifstream input(fixture, std::ios::binary);
    Expect(input.good(), "sanitized follow-wallet fixture must exist");
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

[[nodiscard]] std::string ReadFixture(const char* name) {
    const auto fixture = ExistingSourceFile().parent_path().parent_path().parent_path() /
        "tests" / "contract" / "fixtures" / "gmgn" / name;
    std::ifstream input(fixture, std::ios::binary);
    Expect(input.good(), "sanitized contract fixture must exist");
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

[[nodiscard]] std::filesystem::path ProcessFixturePath() {
    const auto root = ExistingSourceFile().parent_path().parent_path().parent_path();
    const auto solutionOutput = root / "GmemMonitor" / "bin" / "x64" / "Debug" / "GMemMonitor.ProcessFixtures.exe";
    if (std::filesystem::exists(solutionOutput)) return solutionOutput;
    return root / "tests" / "process-fixtures" / "x64" / "Debug" / "GMemMonitor.ProcessFixtures.exe";
}

[[nodiscard]] ProcessResult RunFixture(const std::vector<std::wstring>& arguments, const std::chrono::milliseconds timeout = std::chrono::seconds{2}, const std::size_t stdoutCap = 1024, const std::size_t stderrCap = 1024, const std::stop_token stop = {}) {
    ProcessRunner runner;
    const auto executable = ProcessFixturePath();
    return runner.Run({executable, executable.parent_path(), arguments, timeout, stdoutCap, stderrCap}, stop);
}
}

int main() {
    const auto now = std::chrono::system_clock::time_point{} + std::chrono::seconds(100);
    Expect(EvmAddress::Parse("0xA00000000000000000000000000000000000000F")->ToCanonicalString() ==
        "0xa00000000000000000000000000000000000000f", "EVM addresses must normalize to lowercase hex");
    Expect(!EvmAddress::Parse("0x1234") && !EvmAddress::Parse("0xg000000000000000000000000000000000000000"),
        "malformed EVM addresses must be rejected");
    Expect(MoneyUsd::Parse("99.99")->micros == 99'990'000 && MoneyUsd::Parse("100.000000")->micros == 100'000'000,
        "USD parsing must preserve threshold precision");
    Expect(MoneyUsd::Parse("1.0000000")->micros == 1'000'000 && !MoneyUsd::Parse("1.0000001") &&
        !MoneyUsd::Parse("9223372036854.775808"), "USD parsing must reject non-zero excess precision and overflow");
    MonitoringSettings settings; settings.distinctWalletThreshold = 2; settings.aggregationWindow = std::chrono::seconds(60);
    TokenClusterAggregator aggregator(settings);
    Expect(!aggregator.Add(Event('a', '1', 99'990'000, now, "low"), now), "$99.99 must be rejected");
    Expect(!aggregator.Add(Event('a', '1', 100'000'000, now, "one"), now), "one wallet must not freeze");
    const auto frozen = aggregator.Add(Event('a', '2', 250'000'000, now, "two"), now);
    Expect(frozen && frozen->wallets.size() == 2, "two wallets must freeze once");
    Expect(!aggregator.Add(Event('a', '3', 120'000'000, now, "after-freeze"), now), "triggering cluster must reset");

    FakeClock controllerClock;
    controllerClock.now = now;
    MonitoringController controller(controllerClock);
    AppSettings controllerSettings;
    controllerSettings.distinctWalletThreshold = 2;
    Expect(controller.Start(controllerSettings) && controller.State() == MonitoringState::Authenticating, "controller must begin authenticating with a settings snapshot");
    FollowWalletPage baseline{{Event('d', '1', 100'000'000, now - std::chrono::seconds{1}, "old"), Event('d', '2', 100'000'000, now, "edge")}};
    const auto initialUpdate = controller.HandleInitialPage(baseline);
    Expect(initialUpdate.state == MonitoringState::Monitoring && initialUpdate.frozenClusters.empty(), "old baseline events must not aggregate and exact-start events may enter the session");
    const auto liveUpdate = controller.HandlePollResult(FollowWalletPage{{Event('d', '3', 100'000'000, now, "live")}});
    Expect(liveUpdate.frozenClusters.size() == 1, "new distinct-wallet event must freeze a qualifying controller cluster");
    const auto duplicateUpdate = controller.HandlePollResult(FollowWalletPage{{Event('d', '3', 100'000'000, now, "live")}});
    Expect(duplicateUpdate.frozenClusters.empty(), "controller must deduplicate session events");
    const auto retryUpdate = controller.HandlePollResult(GmgnFailure{GmgnFailureCode::TransientNetworkOrServer, "retry"});
    Expect(retryUpdate.state == MonitoringState::Retrying, "transient GMGN failure must enter retrying state");
    const auto authenticationUpdate = controller.HandlePollResult(GmgnFailure{GmgnFailureCode::Authentication, "auth"});
    Expect(authenticationUpdate.state == MonitoringState::AuthenticationRequired, "authentication failure must stop polling for credentials");
    controller.Stop();
    Expect(controller.State() == MonitoringState::Stopped, "controller stop must clear session state");
    Expect(WalletActivityPoller::RetryDelay(0, 10'000) == std::chrono::seconds{1} && WalletActivityPoller::RetryDelay(5, 10'000) == std::chrono::seconds{30} &&
        WalletActivityPoller::RetryDelay(99, 10'000) == std::chrono::seconds{30} && WalletActivityPoller::RetryDelay(0, 0) == std::chrono::milliseconds{750}, "retry schedule must be bounded and deterministically jittered");
    FrozenClusterQueue queue;
    FrozenTokenCluster queuedCluster{Address('f'), {}, now};
    Expect(queue.TryEnqueue(queuedCluster), "bounded queue must accept a frozen cluster below capacity");
    const auto dequeuedCluster = queue.WaitDequeue({});
    Expect(dequeuedCluster && dequeuedCluster->token == Address('f'), "bounded queue must preserve frozen cluster ownership");
    EventDeduplicator boundedDedupe;
    for (std::size_t index = 0; index <= EventDeduplicator::kCapacity; ++index) {
        Expect(boundedDedupe.InsertIfNew("key-" + std::to_string(index)), "new dedupe entries must be accepted");
    }
    Expect(boundedDedupe.InsertIfNew("key-0") && !boundedDedupe.InsertIfNew("key-1000"),
        "dedupe capacity must evict the least-recent entry and retain recent entries");
    Expect(boundedDedupe.Size() == EventDeduplicator::kCapacity, "dedupe cache must remain at its explicit capacity");

    MonitoringSettings soakSettings;
    soakSettings.distinctWalletThreshold = 100;
    soakSettings.aggregationWindow = std::chrono::seconds{60};
    TokenClusterAggregator soakAggregator(soakSettings);
    EventDeduplicator soakDedupe;
    for (std::size_t index = 0; index < 14'400; ++index) {
        const auto eventTime = now + std::chrono::seconds(index);
        WalletBuyEvent event;
        event.stableKey = "soak-" + std::to_string(index);
        event.token = Address(static_cast<unsigned char>(index % 16 + 1));
        event.wallet = Address(static_cast<unsigned char>((index / 16) % 16 + 32));
        event.amountUsd = {100'000'000};
        event.timestamp = eventTime;
        Expect(soakDedupe.InsertIfNew(event.stableKey), "accelerated soak events must remain unique");
        static_cast<void>(soakAggregator.Add(event, eventTime));
    }
    Expect(soakDedupe.Size() == EventDeduplicator::kCapacity && soakAggregator.ActiveTokenCount() <= 16 &&
        soakAggregator.StoredEventCount() <= 61, "accelerated multi-hour feed must keep dedupe and rolling-window state bounded");

    TokenClusterAggregator representatives(settings);
    static_cast<void>(representatives.Add(Event('b', '3', 120'000'000, now - std::chrono::seconds(30), "120"), now));
    static_cast<void>(representatives.Add(Event('b', '3', 250'000'000, now - std::chrono::seconds(20), "250"), now));
    const auto second = representatives.Add(Event('b', '4', 180'000'000, now, "180"), now);
    const auto representative = second ? std::find_if(second->wallets.begin(), second->wallets.end(), [](const WalletContribution& item) {
        return item.wallet == Address('3');
    }) : std::vector<WalletContribution>::iterator{};
    Expect(second && representative != second->wallets.end() && representative->largestUnexpiredBuy.amountUsd.micros == 250'000'000, "largest wallet buy must win");

    TokenClusterAggregator expiryFallback(settings);
    static_cast<void>(expiryFallback.Add(Event('e', '1', 250'000'000, now - std::chrono::seconds(65), "old-max"), now - std::chrono::seconds(65)));
    static_cast<void>(expiryFallback.Add(Event('e', '1', 180'000'000, now - std::chrono::seconds(10), "new-smaller"), now - std::chrono::seconds(10)));
    const auto fallbackRepresentative = expiryFallback.Add(Event('e', '2', 100'000'000, now, "trigger"), now);
    const auto fallbackWallet = fallbackRepresentative ? std::find_if(fallbackRepresentative->wallets.begin(), fallbackRepresentative->wallets.end(), [](const WalletContribution& item) {
        return item.wallet == Address('1');
    }) : std::vector<WalletContribution>::iterator{};
    Expect(fallbackRepresentative && fallbackWallet != fallbackRepresentative->wallets.end() &&
        fallbackWallet->largestUnexpiredBuy.amountUsd.micros == 180'000'000,
        "an expired maximum must fall back to a later unexpired buy from the same wallet");

    TokenClusterAggregator independent(settings);
    static_cast<void>(independent.Add(Event('6', '1', 100'000'000, now, "x1"), now));
    static_cast<void>(independent.Add(Event('7', '1', 100'000'000, now, "y1"), now));
    const auto frozenX = independent.Add(Event('6', '2', 100'000'000, now, "x2"), now);
    const auto frozenY = independent.Add(Event('7', '2', 100'000'000, now, "y2"), now);
    Expect(frozenX && frozenY && frozenX->token == Address('6') && frozenY->token == Address('7'),
        "independent token clusters must freeze independently");
    const auto& frozenXValue = RequireValue(frozenX, "first independent cluster must be present");
    const auto frozenXSize = frozenXValue.wallets.size();
    static_cast<void>(independent.Add(Event('6', '3', 100'000'000, now, "x3"), now));
    Expect(frozenXValue.wallets.size() == frozenXSize, "frozen cluster representatives must remain immutable");

    TokenClusterAggregator boundaries(settings);
    Expect(!boundaries.Add(Event('c', '5', 100'000'000, now - std::chrono::seconds(60), "edge"), now), "first edge event");
    Expect(boundaries.Add(Event('c', '6', 100'000'000, now, "edge-two"), now).has_value(), "exact cutoff must remain valid");
    TokenClusterAggregator outsideBoundary(settings);
    Expect(!outsideBoundary.Add(Event('8', '1', 100'000'000, now - std::chrono::seconds(60) - std::chrono::system_clock::duration{1}, "too-old"), now), "first expired event");
    Expect(!outsideBoundary.Add(Event('8', '2', 100'000'000, now, "current"), now), "one-tick-old event must be expired");
    Expect(ValidateGmgnUrl("https://gmgn.ai/bsc/token/0x1").has_value(), "GMGN HTTPS URL must be accepted");
    Expect(ValidateGmgnUrl("HTTPS://GMGN.AI/bsc/token/0x1").has_value(), "hostname comparison must be case-insensitive");
    Expect(!ValidateGmgnUrl("http://gmgn.ai/bsc/token/0x1").has_value(), "HTTP URL must be rejected");
    Expect(!ValidateGmgnUrl("https://gmgn.ai.evil.test/x").has_value(), "lookalike host must be rejected");
    Expect(!ValidateGmgnUrl("https://user@gmgn.ai/x").has_value(), "userinfo URL must be rejected");
    std::string embeddedNullUrl{"https://gmgn.ai/ok"};
    embeddedNullUrl.push_back('\0');
    embeddedNullUrl.append("evil");
    Expect(!ValidateGmgnUrl(embeddedNullUrl).has_value(), "embedded NUL URLs must be rejected");
    CooldownManager cooldown; const auto steady = std::chrono::steady_clock::time_point{};
    Expect(!cooldown.IsActive(Address('a'), steady), "fresh cooldown must be inactive");
    cooldown.MarkDelivered(Address('a'), steady);
    Expect(cooldown.IsActive(Address('a'), steady + std::chrono::seconds(599)), "active cooldown must suppress");
    Expect(!cooldown.IsActive(Address('a'), steady + std::chrono::seconds(600)), "expired cooldown must permit");

    FakeAlertClock alertClock;
    FakeNotificationService notifications;
    auto enrichmentClient = std::make_shared<FakeEnrichmentClient>();
    const FrozenTokenCluster enrichmentCluster{Address('a'), {{Address('1'), Event('a', '1', 120'000'000, now, "enrichment-a")}, {Address('2'), Event('a', '2', 250'000'000, now, "enrichment-b")}}, now};
    enrichmentClient->info = {Address('a'), "GOOD", "https://gmgn.ai/bsc/token/example", "0.8"};
    enrichmentClient->security = TokenSecurity{Address('a'), false, false, std::nullopt, "1", "2"};
    GmgnRequestScheduler priorityScheduler;
    auto blockingPermit = priorityScheduler.Acquire(GmgnRequestScheduler::Priority::Enrichment,
                                                    GmgnRequestScheduler::kEnrichmentWeight, {});
    Expect(blockingPermit.has_value(), "scheduler must admit work when its single process slot is free");
    std::mutex admissionMutex;
    std::vector<GmgnRequestScheduler::Priority> admissionOrder;
    std::jthread queuedEnrichment([&](const std::stop_token stop) {
        const auto permit = priorityScheduler.Acquire(GmgnRequestScheduler::Priority::Enrichment,
                                                      GmgnRequestScheduler::kEnrichmentWeight, stop);
        if (permit) { std::scoped_lock lock(admissionMutex); admissionOrder.push_back(GmgnRequestScheduler::Priority::Enrichment); }
    });
    ExpectEventually([&] { return priorityScheduler.PendingCount() == 1; },
                     "enrichment request must queue behind the active process slot");
    std::jthread queuedFeed([&](const std::stop_token stop) {
        const auto permit = priorityScheduler.Acquire(GmgnRequestScheduler::Priority::Feed,
                                                      GmgnRequestScheduler::kFeedWeight, stop);
        if (permit) { std::scoped_lock lock(admissionMutex); admissionOrder.push_back(GmgnRequestScheduler::Priority::Feed); }
    });
    ExpectEventually([&] { return priorityScheduler.PendingCount() == 2; },
                     "feed request must enter the shared bounded scheduler");
    blockingPermit.reset();
    queuedFeed.join();
    queuedEnrichment.join();
    Expect(admissionOrder.size() == 2 && admissionOrder.front() == GmgnRequestScheduler::Priority::Feed,
           "queued feed polling must take priority over queued enrichment");
    Expect(priorityScheduler.ActiveCount() == 0 && priorityScheduler.PendingCount() == 0,
           "scheduler permits must restore the concurrency slot on destruction");

    GmgnRequestScheduler cancelledScheduler;
    auto cancelledBlocker = cancelledScheduler.Acquire(GmgnRequestScheduler::Priority::Feed,
                                                       GmgnRequestScheduler::kFeedWeight, {});
    bool cancelledAdmissionSucceeded{true};
    std::jthread cancelledWaiter([&](const std::stop_token stop) {
        cancelledAdmissionSucceeded = cancelledScheduler.Acquire(
            GmgnRequestScheduler::Priority::Enrichment,
            GmgnRequestScheduler::kEnrichmentWeight, stop).has_value();
    });
    ExpectEventually([&] { return cancelledScheduler.PendingCount() == 1; },
                     "pending scheduler work must be observable before cancellation");
    cancelledScheduler.CancelPending();
    cancelledWaiter.join();
    Expect(!cancelledAdmissionSucceeded && cancelledScheduler.PendingCount() == 0,
           "session cancellation must wake and reject queued GMGN work");
    cancelledBlocker.reset();
    cancelledScheduler.Reset();
    Expect(cancelledScheduler.Acquire(GmgnRequestScheduler::Priority::Feed,
                                      GmgnRequestScheduler::kFeedWeight, {}).has_value(),
           "scheduler reset must permit a clean subsequent monitoring session");

    GmgnRequestScheduler scheduler;
    TokenAnalysisService analysis(enrichmentClient, scheduler, notifications, alertClock);
    const auto enriched = analysis.Analyze(enrichmentCluster, {});
    Expect(enriched.delivered && enriched.alert && notifications.alerts.size() == 1 && enriched.alert->largestQualifyingBuy.micros == 250'000'000,
        "required token info and security must produce one complete alert");
    Expect(enriched.alert->validatedGmgnUrl == "https://gmgn.ai/bsc/token/example" && enriched.alert->risks[2].value == "unavailable",
        "validated GMGN links and unavailable risk facts must remain distinct");
    const auto cooldownSuppressed = analysis.Analyze(enrichmentCluster, {});
    Expect(cooldownSuppressed.suppressedByCooldown && enrichmentClient->infoCalls == 2 && enrichmentClient->securityCalls == 2,
        "cooldown must run after required enrichment rather than suppressing GMGN analysis");
    alertClock.now += std::chrono::seconds{600};
    const auto afterCooldown = analysis.Analyze(enrichmentCluster, {});
    Expect(afterCooldown.delivered && notifications.alerts.size() == 2, "expired cooldown must permit a later independent cluster");
    notifications.accept = false;
    analysis.ClearSession();
    const auto rejectedDelivery = analysis.Analyze(enrichmentCluster, {});
    notifications.accept = true;
    const auto retryAfterRejectedDelivery = analysis.Analyze(enrichmentCluster, {});
    Expect(!rejectedDelivery.delivered && retryAfterRejectedDelivery.delivered, "failed notification delivery must not start a cooldown");
    enrichmentClient->security = GmgnFailure{GmgnFailureCode::MalformedJson, "malformed"};
    analysis.ClearSession();
    const auto partial = analysis.Analyze(enrichmentCluster, {});
    Expect(!partial.delivered && !partial.alert && notifications.alerts.size() == 3, "missing required security data must not produce a partial alert");
    enrichmentClient->security = TokenSecurity{Address('a'), false, false, std::nullopt, "1", "2"};
    enrichmentClient->securityResults.push_back(GmgnFailure{GmgnFailureCode::RateLimited, "rate limited", std::chrono::seconds{0}});
    const auto retryableFailure = analysis.Analyze(enrichmentCluster, {});
    Expect(retryableFailure.retryable && retryableFailure.retryAfter == std::chrono::seconds{0}, "rate-limited enrichment must retain the provider retry delay for executor retry");
    enrichmentClient->securityResults.push_back(GmgnFailure{GmgnFailureCode::TransientNetworkOrServer, "transient", std::chrono::seconds{0}});
    enrichmentClient->securityResults.push_back(TokenSecurity{Address('a'), false, false, std::nullopt, "1", "2"});
    analysis.ClearSession();
    std::mutex completionMutex;
    std::condition_variable completionWake;
    bool retryDelivered{};
    TokenAnalysisExecutor executor(analysis, [&](const AnalysisUpdate& update) {
        if (update.delivered) { std::scoped_lock lock(completionMutex); retryDelivered = true; completionWake.notify_one(); }
    });
    Expect(executor.Start() && executor.Submit(enrichmentCluster), "analysis executor must accept a qualifying frozen cluster");
    { std::unique_lock lock(completionMutex); Expect(completionWake.wait_for(lock, std::chrono::seconds{1}, [&] { return retryDelivered; }), "executor must retain and retry a transient enrichment failure"); }
    executor.Stop();

    std::mutex sessionAuthMutex;
    std::condition_variable sessionAuthWake;
    bool sessionAuthenticationRequired{};
    auto sessionAuthClient = std::make_shared<AuthenticationDuringAnalysisClient>(FollowWalletPage{{
        Event('9', '1', 100'000'000, now, "session-auth-1"),
        Event('9', '2', 100'000'000, now, "session-auth-2")}});
    MonitoringSession authenticationSession(sessionAuthClient, notifications, controllerClock, alertClock,
        [&](const MonitoringUpdate& update) {
            if (update.state == MonitoringState::AuthenticationRequired) {
                std::scoped_lock lock(sessionAuthMutex);
                sessionAuthenticationRequired = true;
                sessionAuthWake.notify_one();
            }
        });
    AppSettings sessionSettings;
    sessionSettings.distinctWalletThreshold = 2;
    Expect(authenticationSession.Start(sessionSettings), "composed monitoring session must start");
    {
        std::unique_lock lock(sessionAuthMutex);
        Expect(sessionAuthWake.wait_for(lock, std::chrono::seconds{2}, [&] { return sessionAuthenticationRequired; }),
            "authentication failure during enrichment must stop feed polling and surface authentication-required state");
    }
    authenticationSession.Stop();

    const auto settingsDirectory = std::filesystem::temp_directory_path() / "GMemMonitor-settings-self-test";
    const auto settingsPath = settingsDirectory / "config.json";
    std::error_code cleanupError;
    std::filesystem::remove_all(settingsDirectory, cleanupError);
    SettingsStore settingsStore(settingsPath);
    const auto missingSettings = settingsStore.Load();
    Expect(!missingSettings.warning && missingSettings.settings.pollInterval == std::chrono::seconds{20}, "missing settings must use defaults");
    AppSettings tooFrequentSettings;
    tooFrequentSettings.pollInterval = std::chrono::seconds{19};
    Expect(ValidateSettings(tooFrequentSettings).has_value(), "polling faster than the 20-second GMGN rate-policy floor must be rejected");
    AppSettings savedSettings;
    savedSettings.pollInterval = std::chrono::seconds{30};
    savedSettings.minimumBuyUsd = {123'456'789};
    Expect(!settingsStore.Save(savedSettings), "valid non-secret settings must save atomically");
    const auto reloadedSettings = settingsStore.Load();
    Expect(!reloadedSettings.warning && reloadedSettings.settings.pollInterval == std::chrono::seconds{30} && reloadedSettings.settings.minimumBuyUsd.micros == 123'456'789, "settings must round-trip exactly");
    { std::ofstream corrupt(settingsPath, std::ios::binary | std::ios::trunc); corrupt << "{\"schema_version\":999}"; }
    const auto corruptSettings = settingsStore.Load();
    Expect(corruptSettings.warning && corruptSettings.settings.pollInterval == std::chrono::seconds{20}, "invalid settings must retain defaults with a warning");
    Expect(std::filesystem::exists(settingsPath), "invalid settings must be preserved for diagnosis");
    std::filesystem::remove_all(settingsDirectory, cleanupError);

    Expect(std::filesystem::is_regular_file(ProcessFixturePath()), "process fixture executable must be built");
    const auto validProcess = RunFixture({L"valid-json"});
    Expect(validProcess.reason == ProcessTerminationReason::Completed && validProcess.stdoutOutput.text == "{\"ok\":true}\n", "valid fixture JSON must be captured");
    const auto partialProcess = RunFixture({L"partial-writes"});
    Expect(partialProcess.reason == ProcessTerminationReason::Completed && partialProcess.stdoutOutput.totalBytes == 128, "partial pipe writes must be captured completely");
    const auto warningProcess = RunFixture({L"warning"});
    Expect(warningProcess.reason == ProcessTerminationReason::Completed && warningProcess.stderrOutput.text == "warning\n", "zero-exit warnings must remain separate stderr");
    const auto nonzeroProcess = RunFixture({L"nonzero"});
    Expect(nonzeroProcess.reason == ProcessTerminationReason::Completed && nonzeroProcess.exitCode == 7, "non-zero exit must be observable");
    SetEnvironmentVariableW(L"GMEMMONITOR_TEST_SECRET", L"must-not-be-inherited");
    const auto sanitizedEnvironment = RunFixture({L"sanitized-environment"});
    SetEnvironmentVariableW(L"GMEMMONITOR_TEST_SECRET", nullptr);
    Expect(sanitizedEnvironment.reason == ProcessTerminationReason::Completed && sanitizedEnvironment.exitCode == 0 &&
        sanitizedEnvironment.stdoutOutput.text == "clean\n", "child stdin and inherited environment must be sanitized");
    const auto stdoutOverflow = RunFixture({L"large-stdout"}, std::chrono::seconds{2}, 1024);
    Expect(stdoutOverflow.reason == ProcessTerminationReason::OutputLimitExceeded && stdoutOverflow.stdoutOutput.truncated, "stdout over the cap must terminate safely");
    const auto stderrOverflow = RunFixture({L"large-stderr"}, std::chrono::seconds{2}, 1024, 1024);
    Expect(stderrOverflow.reason == ProcessTerminationReason::OutputLimitExceeded && stderrOverflow.stderrOutput.truncated, "stderr over the cap must terminate safely");
    const auto timedOutProcess = RunFixture({L"hang"}, std::chrono::milliseconds{100});
    Expect(timedOutProcess.reason == ProcessTerminationReason::TimedOut && timedOutProcess.duration < std::chrono::seconds{1},
        "hung child must time out and shut down within the documented local bound");
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto cancelledProcess = RunFixture({L"hang"}, std::chrono::seconds{2}, 1024, 1024, cancellation.get_token());
    Expect(cancelledProcess.reason == ProcessTerminationReason::Cancelled, "pre-cancelled child must stop promptly");
    const auto processDirectory = std::filesystem::temp_directory_path() / "GMemMonitor-process-self-test";
    const auto childMarker = processDirectory / "child.pid";
    std::filesystem::remove_all(processDirectory, cleanupError);

    const auto logDirectory = std::filesystem::temp_directory_path() / "GMemMonitor-log-self-test";
    std::filesystem::remove_all(logDirectory, cleanupError);
    LoggingService logger(logDirectory / "gmemmonitor.log", 96, 3);
    Expect(logger.Initialize(), "bounded diagnostic logger must initialize");
    logger.Write(LogLevel::Info, "startup");
    for (int index = 0; index < 12; ++index) logger.Write(LogLevel::Warning, "retrying", "bounded diagnostic");
    logger.Write(LogLevel::Error, "authentication error", "GMGN_API_KEY=must-never-appear");
    logger.Flush();
    Expect(std::filesystem::exists(logDirectory / "gmemmonitor.log"), "active diagnostic log must exist");
    Expect(std::filesystem::exists(logDirectory / "gmemmonitor.log.1"), "diagnostic logs must rotate when bounded size is reached");
    Expect(std::distance(std::filesystem::directory_iterator(logDirectory), std::filesystem::directory_iterator{}) <= 3,
        "diagnostic log retention must remain bounded");
    std::string combinedLogs;
    for (const auto& entry : std::filesystem::directory_iterator(logDirectory)) {
        std::ifstream input(entry.path(), std::ios::binary);
        combinedLogs.append(std::istreambuf_iterator<char>(input), {});
    }
    Expect(combinedLogs.find("must-never-appear") == std::string::npos && combinedLogs.find("[redacted diagnostic]") != std::string::npos,
        "diagnostic logging must redact credential-shaped text");
    std::filesystem::remove_all(logDirectory, cleanupError);
    std::filesystem::create_directories(processDirectory, cleanupError);
    const auto spawnedProcess = RunFixture({L"spawn-child", childMarker.wstring()}, std::chrono::milliseconds{300});
    Expect(spawnedProcess.reason == ProcessTerminationReason::TimedOut, "parent with a spawned child must time out");
    std::ifstream childPidFile(childMarker);
    unsigned long childPid{};
    childPidFile >> childPid;
    Expect(childPid != 0, "spawned child must report its process identifier");
    HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, childPid);
    Expect(!child || WaitForSingleObject(child, 0) == WAIT_OBJECT_0, "Job Object must terminate spawned child processes");
    if (child) CloseHandle(child);
    std::filesystem::remove_all(processDirectory, cleanupError);

    const std::string followWalletFixture = ReadFollowWalletFixture();
    const auto parsedFixture = ParseFollowWalletPageJson(followWalletFixture);
    const auto& parsedPage = RequireAlternative<FollowWalletPage>(parsedFixture,
        "sanitized follow-wallet fixture must parse into events");
    Expect(!parsedPage.events.empty(), "sanitized follow-wallet fixture must contain events");
    Expect(parsedPage.rejectedRecordCount == 0, "captured fixture records must satisfy the required contract");
    Expect(parsedPage.nextPageToken.has_value(), "captured fixture must preserve the pagination-token type");
    const auto& parsedEvent = parsedPage.events.front();
    Expect(parsedEvent.amountUsd.micros == 100'000'000, "numeric USD amount must parse as fixed-point micro-USD");
    const auto fixtureAddress = EvmAddress::Parse("0x1111111111111111111111111111111111111111");
    Expect(fixtureAddress && parsedEvent.wallet == *fixtureAddress && parsedEvent.token == *fixtureAddress, "fixture addresses must normalize");
    Expect(parsedEvent.chain == Chain::Bsc && parsedEvent.side == TradeSide::Buy, "only BSC BUY records are accepted");

    const auto emptyPage = ParseFollowWalletPageJson(R"({"list":[]})");
    const auto* empty = std::get_if<FollowWalletPage>(&emptyPage);
    Expect(empty && empty->events.empty() && empty->rejectedRecordCount == 0, "empty feeds must be accepted");
    const auto malformedPage = ParseFollowWalletPageJson("{\"list\":[}");
    const auto* malformedFailure = std::get_if<GmgnFailure>(&malformedPage);
    Expect(malformedFailure && malformedFailure->code == GmgnFailureCode::MalformedJson, "malformed JSON must be rejected safely");
    const auto invalidRecord = ParseFollowWalletPageJson(R"({"list":[{"chain":"bsc","side":"buy"}]})");
    const auto* rejected = std::get_if<FollowWalletPage>(&invalidRecord);
    Expect(rejected && rejected->events.empty() && rejected->rejectedRecordCount == 1, "invalid records must not enter aggregation");
    const auto stringNumericRecord = ParseFollowWalletPageJson(
        R"({"list":[{"id":"test-id","chain":"BSC","side":"BUY","transaction_hash":"0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","maker":"0x1111111111111111111111111111111111111111","base_address":"0x2222222222222222222222222222222222222222","amount_usd":"100.000001","base_amount":"1.25","price_usd":"0.000000001","timestamp":"1700000000","base_token":{"symbol":"A\nB"},"unknown":{"future":true}}]})");
    const auto& stringNumeric = RequireAlternative<FollowWalletPage>(stringNumericRecord,
        "numeric-string fields and unknown fields must parse");
    Expect(stringNumeric.events.size() == 1, "numeric-string fields and unknown fields must produce one record");
    Expect(stringNumeric.events.front().amountUsd.micros == 100'000'001, "USD numeric strings must retain micro-USD precision");
    Expect(stringNumeric.events.front().sanitizedSymbol == "AB", "display symbols must remove control characters");
    Expect(SanitizeDisplayText("ABC" "\xE2\x80\xAE" "def") == "ABCdef", "display symbols must remove bidirectional formatting characters");
    Expect(SanitizeDisplayText("A" "\xD8\x9C" "B" "\xE2\x80\x8E" "C" "\xE2\x80\x8F" "D") == "ABCD",
        "display symbols must remove Arabic letter mark and left-to-right/right-to-left marks");
    Expect(SanitizeDisplayText("A" "\xF0\x28\x8C\x28" "B") == "A((B", "display symbols must discard malformed UTF-8 safely");
    Expect(SanitizeDisplayText("\xC3\xA9", 1).empty(), "display cap must not split a UTF-8 sequence");
    const auto fallbackKeyRecord = ParseFollowWalletPageJson(
        R"({"list":[{"chain":"bsc","side":"buy","transaction_hash":"0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","maker":"0x1111111111111111111111111111111111111111","base_address":"0x2222222222222222222222222222222222222222","amount_usd":"100.0","base_amount":"001.2500","price_usd":"1","timestamp":"1700000000"}]})");
    const auto* fallbackPage = std::get_if<FollowWalletPage>(&fallbackKeyRecord);
    Expect(fallbackPage && fallbackPage->events.size() == 1 && fallbackPage->events.front().stableKey.rfind("fallback:", 0) == 0,
        "records without a GMGN id must receive a SHA-256 fallback key");
    const auto emptyIdFallbackRecord = ParseFollowWalletPageJson(
        R"({"list":[{"id":"","chain":"bsc","side":"buy","transaction_hash":"0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","maker":"0x1111111111111111111111111111111111111111","base_address":"0x2222222222222222222222222222222222222222","amount_usd":"100","base_amount":".5","price_usd":"1","timestamp":"1700000000"}]})");
    const auto* emptyIdPage = std::get_if<FollowWalletPage>(&emptyIdFallbackRecord);
    Expect(emptyIdPage && emptyIdPage->events.size() == 1 && emptyIdPage->events.front().stableKey.rfind("fallback:", 0) == 0,
        "empty GMGN ids and leading-dot decimals must use a safe fallback key");
    const auto invalidIdFallbackRecord = ParseFollowWalletPageJson(
        R"({"list":[{"id":"bad id\u000a","chain":"bsc","side":"buy","transaction_hash":"0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","maker":"0x1111111111111111111111111111111111111111","base_address":"0x2222222222222222222222222222222222222222","amount_usd":"100","base_amount":"1","price_usd":"1","timestamp":"1700000000"}]})");
    const auto* invalidIdPage = std::get_if<FollowWalletPage>(&invalidIdFallbackRecord);
    Expect(invalidIdPage && invalidIdPage->events.size() == 1 && invalidIdPage->events.front().gmgnRecordId.empty() &&
        invalidIdPage->events.front().stableKey.rfind("fallback:", 0) == 0, "invalid GMGN ids must use the canonical fallback key");
    const auto negativeUsdRecord = ParseFollowWalletPageJson(
        R"({"list":[{"chain":"bsc","side":"buy","transaction_hash":"0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","maker":"0x1111111111111111111111111111111111111111","base_address":"0x2222222222222222222222222222222222222222","amount_usd":"-1","base_amount":"1","price_usd":"1","timestamp":"1700000000"}]})");
    Expect(std::get<FollowWalletPage>(negativeUsdRecord).rejectedRecordCount == 1, "negative USD activity must be rejected");
    const auto timestampOverflowRecord = ParseFollowWalletPageJson(
        R"({"list":[{"chain":"bsc","side":"buy","transaction_hash":"0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","maker":"0x1111111111111111111111111111111111111111","base_address":"0x2222222222222222222222222222222222222222","amount_usd":"100","base_amount":"1","price_usd":"1","timestamp":"9223372036854775807"}]})");
    Expect(std::get<FollowWalletPage>(timestampOverflowRecord).rejectedRecordCount == 1, "timestamps outside system_clock range must be rejected");

    const auto tokenInfo = ParseTokenInfoJson(ReadFixture("token_info_bsc.json"));
    const auto* info = std::get_if<TokenInfo>(&tokenInfo);
    Expect(info && info->sanitizedSymbol == "redacted" && info->lockedRatio == "1", "token-info fixture must parse typed metadata");
    const auto emptySymbolInfo = ParseTokenInfoJson(R"({"address":"0x1111111111111111111111111111111111111111","symbol":"","link":{"gmgn":null},"locked_ratio":null,"future":true})");
    Expect(std::get_if<TokenInfo>(&emptySymbolInfo) && std::get<TokenInfo>(emptySymbolInfo).sanitizedSymbol.empty(),
        "empty optional display symbols and null optional facts must remain usable with address fallback");
    const auto tokenSecurity = ParseTokenSecurityJson(ReadFixture("token_security_bsc.json"));
    const auto* security = std::get_if<TokenSecurity>(&tokenSecurity);
    Expect(security && security->honeypot == false && security->openSource == false && security->renounced == false,
        "token-security fixture must preserve explicit negative risk facts");

    auto processRunner = std::make_shared<FakeProcessRunner>();
    processRunner->result.reason = ProcessTerminationReason::Completed;
    processRunner->result.exitCode = 1;
    processRunner->result.stderrOutput.text =
        "HTTP 429 code=429 error=RATE_LIMIT_EXCEEDED message=IP rate limit exceeded (~28s remaining).";
    const auto sourceFile = ExistingSourceFile();
    Expect(std::filesystem::is_regular_file(sourceFile), "test source file must exist");
    GmgnCliClient rateLimitedClient({sourceFile, sourceFile}, processRunner);
    const auto firstRateLimit = rateLimitedClient.FetchFollowWalletBuys({});
    const auto* firstFailure = std::get_if<GmgnFailure>(&firstRateLimit);
    Expect(firstFailure && firstFailure->code == GmgnFailureCode::RateLimited && firstFailure->retryAfter == std::chrono::seconds{30}, "HTTP 429 must be classified with its bounded retry delay");
    const auto suppressedRetry = rateLimitedClient.FetchFollowWalletBuys({});
    const auto* retryFailure = std::get_if<GmgnFailure>(&suppressedRetry);
    Expect(retryFailure && retryFailure->code == GmgnFailureCode::RateLimited, "active rate limit must suppress retry");
    Expect(processRunner->callCount == 1, "active rate limit must not start a second process");

    processRunner->result = {};
    processRunner->result.reason = ProcessTerminationReason::Completed;
    processRunner->result.exitCode = 1;
    processRunner->result.stderrOutput.text = "HTTP 401 unauthorized";
    GmgnCliClient authenticationClient({sourceFile, sourceFile}, processRunner);
    const auto authenticationFailure = authenticationClient.FetchFollowWalletBuys({});
    const auto* classifiedAuthenticationFailure = std::get_if<GmgnFailure>(&authenticationFailure);
    Expect(classifiedAuthenticationFailure && classifiedAuthenticationFailure->code == GmgnFailureCode::Authentication,
        "CLI HTTP 401 must stop monitoring as an authentication failure");

    processRunner->result = {};
    processRunner->result.reason = ProcessTerminationReason::Completed;
    processRunner->result.exitCode = 0;
    processRunner->result.stdoutOutput.text = followWalletFixture;
    GmgnCliClient fixtureClient({sourceFile, sourceFile}, processRunner);
    const auto fixtureResult = fixtureClient.FetchFollowWalletBuys({});
    const auto* clientPage = std::get_if<FollowWalletPage>(&fixtureResult);
    Expect(clientPage && !clientPage->events.empty(), "GMGN client must return the typed parsed page after a successful process result");
    std::cout << "Monitoring self-tests passed.\n";
}
