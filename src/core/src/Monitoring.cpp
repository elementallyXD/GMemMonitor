#include "gmemmonitor/core/Monitoring.h"
#include "gmemmonitor/core/Analysis.h"

#include <algorithm>

namespace gmemmonitor::core {

bool EventDeduplicator::InsertIfNew(std::string key) {
    if (key.empty()) return false;
    if (const auto found = entries_.find(key); found != entries_.end()) { lru_.splice(lru_.begin(), lru_, found->second); return false; }
    lru_.push_front(std::move(key)); entries_.emplace(lru_.front(), lru_.begin());
    if (entries_.size() > kCapacity) { entries_.erase(lru_.back()); lru_.pop_back(); }
    return true;
}

void EventDeduplicator::Clear() noexcept { entries_.clear(); lru_.clear(); }

TokenClusterAggregator::TokenClusterAggregator(MonitoringSettings settings) : settings_(settings) {}

std::optional<FrozenTokenCluster> TokenClusterAggregator::Add(const WalletBuyEvent& event, const std::chrono::system_clock::time_point now) {
    if (event.chain != Chain::Bsc || event.side != TradeSide::Buy || event.amountUsd.micros < settings_.minimumBuyUsd.micros) return std::nullopt;
    const std::string token = event.token.ToCanonicalString(); const std::string wallet = event.wallet.ToCanonicalString();
    auto& events = active_[token][wallet];
    const auto position = std::upper_bound(events.begin(), events.end(), event, [](const WalletBuyEvent& left, const WalletBuyEvent& right) { return left.timestamp < right.timestamp; });
    events.insert(position, event); Prune(now); return FreezeIfQualified(token, now);
}

void TokenClusterAggregator::Prune(const std::chrono::system_clock::time_point now) {
    const auto cutoff = now - settings_.aggregationWindow;
    for (auto token = active_.begin(); token != active_.end();) {
        for (auto wallet = token->second.begin(); wallet != token->second.end();) {
            auto& events = wallet->second;
            events.erase(std::remove_if(events.begin(), events.end(), [&](const WalletBuyEvent& event) { return event.timestamp < cutoff; }), events.end());
            if (events.empty()) wallet = token->second.erase(wallet); else ++wallet;
        }
        if (token->second.empty()) token = active_.erase(token); else ++token;
    }
}

std::optional<FrozenTokenCluster> TokenClusterAggregator::FreezeIfQualified(const std::string& token, const std::chrono::system_clock::time_point now) {
    const auto found = active_.find(token); if (found == active_.end() || found->second.size() < settings_.distinctWalletThreshold) return std::nullopt;
    FrozenTokenCluster frozen; const auto parsedToken = EvmAddress::Parse(token); if (!parsedToken) return std::nullopt; frozen.token = *parsedToken; frozen.triggeredAt = now;
    for (const auto& [walletText, events] : found->second) {
        const auto best = std::min_element(events.begin(), events.end(), [](const WalletBuyEvent& left, const WalletBuyEvent& right) {
            if (left.amountUsd.micros != right.amountUsd.micros) return left.amountUsd.micros > right.amountUsd.micros;
            if (left.timestamp != right.timestamp) return left.timestamp < right.timestamp;
            return left.stableKey < right.stableKey;
        });
        if (const auto wallet = EvmAddress::Parse(walletText); wallet && best != events.end()) frozen.wallets.push_back({*wallet, *best});
    }
    active_.erase(found); return frozen;
}

void TokenClusterAggregator::Clear() noexcept { active_.clear(); }

MonitoringController::MonitoringController(IClock& clock) : clock_(clock) {}

bool MonitoringController::Start(const AppSettings& settings) {
    if (state_ != MonitoringState::Stopped && state_ != MonitoringState::AuthenticationRequired) return false;
    if (ValidateSettings(settings)) return false;
    startedAt_ = clock_.UtcNow();
    deduplicator_.emplace();
    aggregator_.emplace(ToMonitoringSettings(settings));
    state_ = MonitoringState::Authenticating;
    return true;
}

MonitoringUpdate MonitoringController::HandleInitialPage(const FollowWalletPage& page) {
    if (state_ != MonitoringState::Authenticating) return {state_, {}, "Initial GMGN feed was not expected."};
    return ProcessPage(page, true);
}

MonitoringUpdate MonitoringController::HandleInitialResult(const GmgnResult<FollowWalletPage>& result) {
    if (const auto* failure = std::get_if<GmgnFailure>(&result)) {
        if (state_ != MonitoringState::Authenticating) return {state_, {}, "Initial GMGN feed was not expected."};
        if (failure->code == GmgnFailureCode::Authentication) state_ = MonitoringState::AuthenticationRequired;
        else if (failure->code != GmgnFailureCode::Cancelled) state_ = MonitoringState::Retrying;
        return {state_, {}, failure->diagnostic, failure->retryAfter};
    }
    return HandleInitialPage(std::get<FollowWalletPage>(result));
}

MonitoringUpdate MonitoringController::HandlePollResult(const GmgnResult<FollowWalletPage>& result) {
    if (state_ != MonitoringState::Monitoring && state_ != MonitoringState::Retrying) return {state_, {}, "GMGN feed result was not expected."};
    if (const auto* failure = std::get_if<GmgnFailure>(&result)) {
        if (failure->code == GmgnFailureCode::Authentication) state_ = MonitoringState::AuthenticationRequired;
        else if (failure->code != GmgnFailureCode::Cancelled) state_ = MonitoringState::Retrying;
        return {state_, {}, failure->diagnostic, failure->retryAfter};
    }
    return ProcessPage(std::get<FollowWalletPage>(result), false);
}

void MonitoringController::Stop() noexcept {
    deduplicator_.reset();
    aggregator_.reset();
    startedAt_.reset();
    state_ = MonitoringState::Stopped;
}

MonitoringState MonitoringController::State() const noexcept { return state_; }

MonitoringUpdate MonitoringController::ProcessPage(const FollowWalletPage& page, const bool initial) {
    std::vector<FrozenTokenCluster> frozen;
    const auto now = clock_.UtcNow();
    for (const auto& event : page.events) {
        if (event.stableKey.empty() || !deduplicator_->InsertIfNew(event.stableKey)) continue;
        // Old records establish the baseline only; events at the exact start are live.
        if (initial && event.timestamp < *startedAt_) continue;
        if (event.timestamp < *startedAt_) continue;
        if (const auto cluster = aggregator_->Add(event, now)) frozen.push_back(*cluster);
    }
    aggregator_->Prune(now);
    state_ = MonitoringState::Monitoring;
    return {state_, std::move(frozen), {}};
}

MonitoringSettings MonitoringController::ToMonitoringSettings(const AppSettings& settings) noexcept {
    return {settings.minimumBuyUsd, settings.distinctWalletThreshold, settings.aggregationWindow};
}

WalletActivityPoller::WalletActivityPoller(std::shared_ptr<IGmgnClient> client, MonitoringController& controller, UpdateHandler handler, GmgnRequestScheduler* scheduler, FrozenClusterHandler frozenClusterHandler)
    : client_(std::move(client)), controller_(controller), handler_(std::move(handler)), scheduler_(scheduler), frozenClusterHandler_(std::move(frozenClusterHandler)) {}

WalletActivityPoller::~WalletActivityPoller() { Stop(); }

bool WalletActivityPoller::Start(const AppSettings& settings) {
    if (!client_ || worker_.joinable() || !controller_.Start(settings)) return false;
    worker_ = std::jthread([this, interval = settings.pollInterval](const std::stop_token stop) { Run(stop, interval); });
    return true;
}

void WalletActivityPoller::Stop() noexcept {
    if (worker_.joinable()) {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
    controller_.Stop();
}

std::chrono::milliseconds WalletActivityPoller::RetryDelay(const std::size_t consecutiveFailures, const std::uint32_t jitterBasisPoints) noexcept {
    constexpr std::array<std::chrono::seconds, 6> delays{std::chrono::seconds{1}, std::chrono::seconds{2}, std::chrono::seconds{4}, std::chrono::seconds{8}, std::chrono::seconds{16}, std::chrono::seconds{30}};
    const auto maximum = std::chrono::duration_cast<std::chrono::milliseconds>(delays[(std::min)(consecutiveFailures, delays.size() - 1)]);
    const auto boundedJitter = (std::min)(jitterBasisPoints, 10'000U);
    // Jitter is in [75%, 100%] of the documented maximum delay.
    return maximum * (7'500 + boundedJitter / 4) / 10'000;
}

void WalletActivityPoller::Run(const std::stop_token stop, const std::chrono::seconds pollInterval) {
    const auto initialStarted = std::chrono::steady_clock::now();
    const auto fetch = [this, stop]() -> GmgnResult<FollowWalletPage> {
        if (!scheduler_) return client_->FetchFollowWalletBuys(stop);
        const auto permit = scheduler_->Acquire(GmgnRequestScheduler::Priority::Feed, stop);
        return permit ? client_->FetchFollowWalletBuys(stop) : GmgnFailure{GmgnFailureCode::Cancelled, "GMGN feed request was cancelled before admission."};
    };
    auto initial = fetch();
    auto initialUpdate = controller_.HandleInitialResult(initial);
    Publish(initialUpdate);
    if (stop.stop_requested() || controller_.State() == MonitoringState::AuthenticationRequired) return;
    std::size_t failures{};
    std::optional<std::chrono::milliseconds> forcedDelay;
    auto nextHealthyDeadline = initialStarted + pollInterval;
    if (initialUpdate.retryAfter) forcedDelay = std::chrono::duration_cast<std::chrono::milliseconds>(*initialUpdate.retryAfter);
    while (!stop.stop_requested()) {
        const bool retrying = controller_.State() == MonitoringState::Retrying;
        jitterState_ = jitterState_ * 1'664'525U + 1'013'904'223U;
        const auto healthyDelay = std::chrono::duration_cast<std::chrono::milliseconds>((std::max)(nextHealthyDeadline - std::chrono::steady_clock::now(), std::chrono::steady_clock::duration::zero()));
        const auto delay = forcedDelay.value_or(retrying ? RetryDelay(failures++, jitterState_ % 10'001U) : healthyDelay);
        forcedDelay.reset();
        std::mutex mutex;
        std::unique_lock lock(mutex);
        if (wake_.wait_for(lock, stop, delay, [] { return false; })) return;
        if (stop.stop_requested()) return;
        const auto requestStarted = std::chrono::steady_clock::now();
        auto result = fetch();
        const auto update = controller_.HandlePollResult(result);
        Publish(update);
        if (update.state == MonitoringState::AuthenticationRequired || update.state == MonitoringState::Stopped) return;
        if (update.state == MonitoringState::Monitoring) {
            failures = 0;
            nextHealthyDeadline = requestStarted + pollInterval;
        }
        if (update.retryAfter) forcedDelay = std::chrono::duration_cast<std::chrono::milliseconds>(*update.retryAfter);
    }
}

void WalletActivityPoller::Publish(const MonitoringUpdate& update) const {
    if (frozenClusterHandler_) {
        for (const auto& cluster : update.frozenClusters) {
            // The bounded analysis executor owns a copy. If it is saturated, the
            // status sink still receives the cluster and can present a visible error.
            static_cast<void>(frozenClusterHandler_(cluster));
        }
    }
    if (handler_) handler_(update);
}

bool FrozenClusterQueue::TryEnqueue(FrozenTokenCluster cluster) {
    std::scoped_lock lock(mutex_);
    if (clusters_.size() == kCapacity) return false;
    clusters_.push_back(std::move(cluster));
    wake_.notify_one();
    return true;
}

std::optional<FrozenTokenCluster> FrozenClusterQueue::WaitDequeue(const std::stop_token stop) {
    std::unique_lock lock(mutex_);
    wake_.wait(lock, stop, [this] { return !clusters_.empty(); });
    if (stop.stop_requested() || clusters_.empty()) return std::nullopt;
    FrozenTokenCluster next = std::move(clusters_.front());
    clusters_.pop_front();
    return next;
}

void FrozenClusterQueue::Clear() noexcept {
    std::scoped_lock lock(mutex_);
    clusters_.clear();
    wake_.notify_all();
}

} // namespace gmemmonitor::core
