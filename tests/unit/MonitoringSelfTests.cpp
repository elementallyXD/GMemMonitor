#include "gmemmonitor/core/Alerts.h"
#include "gmemmonitor/core/Analysis.h"
#include "gmemmonitor/core/GmgnClient.h"
#include "gmemmonitor/core/Monitoring.h"
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
[[nodiscard]] WalletBuyEvent Event(char token, char wallet, std::int64_t usdMicros, std::chrono::system_clock::time_point timestamp, std::string key) {
    return {std::move(key), {}, Address(wallet), Address(token), {}, {usdMicros}, {}, {}, timestamp};
}
void Expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }

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
    return ExistingSourceFile().parent_path().parent_path().parent_path() / "GmemMonitor" / "bin" / "x64" / "Debug" / "GMemMonitor.ProcessFixtures.exe";
}

[[nodiscard]] ProcessResult RunFixture(const std::vector<std::wstring>& arguments, const std::chrono::milliseconds timeout = std::chrono::seconds{2}, const std::size_t stdoutCap = 1024, const std::size_t stderrCap = 1024, const std::stop_token stop = {}) {
    ProcessRunner runner;
    const auto executable = ProcessFixturePath();
    return runner.Run({executable, executable.parent_path(), arguments, timeout, stdoutCap, stderrCap}, stop);
}
}

int main() {
    const auto now = std::chrono::system_clock::time_point{} + std::chrono::seconds(100);
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

    TokenClusterAggregator representatives(settings);
    static_cast<void>(representatives.Add(Event('b', '3', 120'000'000, now - std::chrono::seconds(30), "120"), now));
    static_cast<void>(representatives.Add(Event('b', '3', 250'000'000, now - std::chrono::seconds(20), "250"), now));
    const auto second = representatives.Add(Event('b', '4', 180'000'000, now, "180"), now);
    const auto representative = second ? std::find_if(second->wallets.begin(), second->wallets.end(), [](const WalletContribution& item) {
        return item.wallet == Address('3');
    }) : std::vector<WalletContribution>::iterator{};
    Expect(second && representative != second->wallets.end() && representative->largestUnexpiredBuy.amountUsd.micros == 250'000'000, "largest wallet buy must win");

    TokenClusterAggregator boundaries(settings);
    Expect(!boundaries.Add(Event('c', '5', 100'000'000, now - std::chrono::seconds(60), "edge"), now), "first edge event");
    Expect(boundaries.Add(Event('c', '6', 100'000'000, now, "edge-two"), now).has_value(), "exact cutoff must remain valid");
    Expect(ValidateGmgnUrl("https://gmgn.ai/bsc/token/0x1").has_value(), "GMGN HTTPS URL must be accepted");
    Expect(ValidateGmgnUrl("HTTPS://GMGN.AI/bsc/token/0x1").has_value(), "hostname comparison must be case-insensitive");
    Expect(!ValidateGmgnUrl("http://gmgn.ai/bsc/token/0x1").has_value(), "HTTP URL must be rejected");
    Expect(!ValidateGmgnUrl("https://gmgn.ai.evil.test/x").has_value(), "lookalike host must be rejected");
    Expect(!ValidateGmgnUrl("https://user@gmgn.ai/x").has_value(), "userinfo URL must be rejected");
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
    const auto stdoutOverflow = RunFixture({L"large-stdout"}, std::chrono::seconds{2}, 1024);
    Expect(stdoutOverflow.reason == ProcessTerminationReason::OutputLimitExceeded && stdoutOverflow.stdoutOutput.truncated, "stdout over the cap must terminate safely");
    const auto stderrOverflow = RunFixture({L"large-stderr"}, std::chrono::seconds{2}, 1024, 1024);
    Expect(stderrOverflow.reason == ProcessTerminationReason::OutputLimitExceeded && stderrOverflow.stderrOutput.truncated, "stderr over the cap must terminate safely");
    const auto timedOutProcess = RunFixture({L"hang"}, std::chrono::milliseconds{100});
    Expect(timedOutProcess.reason == ProcessTerminationReason::TimedOut, "hung child must time out");
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto cancelledProcess = RunFixture({L"hang"}, std::chrono::seconds{2}, 1024, 1024, cancellation.get_token());
    Expect(cancelledProcess.reason == ProcessTerminationReason::Cancelled, "pre-cancelled child must stop promptly");
    const auto processDirectory = std::filesystem::temp_directory_path() / "GMemMonitor-process-self-test";
    const auto childMarker = processDirectory / "child.pid";
    std::filesystem::remove_all(processDirectory, cleanupError);
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
    const auto* parsedPage = std::get_if<FollowWalletPage>(&parsedFixture);
    Expect(parsedPage && !parsedPage->events.empty(), "sanitized follow-wallet fixture must parse into events");
    Expect(parsedPage->rejectedRecordCount == 0, "captured fixture records must satisfy the required contract");
    Expect(parsedPage->nextPageToken.has_value(), "captured fixture must preserve the pagination-token type");
    const auto& parsedEvent = parsedPage->events.front();
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
    const auto* stringNumeric = std::get_if<FollowWalletPage>(&stringNumericRecord);
    Expect(stringNumeric && stringNumeric->events.size() == 1, "numeric-string fields and unknown fields must parse");
    Expect(stringNumeric->events.front().amountUsd.micros == 100'000'001, "USD numeric strings must retain micro-USD precision");
    Expect(stringNumeric->events.front().sanitizedSymbol == "AB", "display symbols must remove control characters");
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

    const auto tokenInfo = ParseTokenInfoJson(ReadFixture("token_info_bsc.json"));
    const auto* info = std::get_if<TokenInfo>(&tokenInfo);
    Expect(info && info->sanitizedSymbol == "redacted" && info->lockedRatio == "1", "token-info fixture must parse typed metadata");
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
