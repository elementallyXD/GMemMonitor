#pragma once

#include "gmemmonitor/core/Domain.h"
#include "gmemmonitor/core/GmgnClient.h"
#include "gmemmonitor/core/Settings.h"

#include <chrono>
#include <array>
#include <deque>
#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <unordered_map>

namespace gmemmonitor::core {

enum class MonitoringState { Stopped, Authenticating, Monitoring, Retrying, AuthenticationRequired };

struct IClock {
    virtual ~IClock() = default;
    [[nodiscard]] virtual std::chrono::system_clock::time_point UtcNow() const = 0;
};

struct MonitoringUpdate final {
    MonitoringState state{MonitoringState::Stopped};
    std::vector<FrozenTokenCluster> frozenClusters;
    std::string diagnostic;
    std::optional<std::chrono::seconds> retryAfter;
};

struct MonitoringSettings final {
    MoneyUsd minimumBuyUsd{100'000'000};
    std::size_t distinctWalletThreshold{5};
    std::chrono::seconds aggregationWindow{60};
};

class EventDeduplicator final {
public:
    static constexpr std::size_t kCapacity = 1'000;
    [[nodiscard]] bool InsertIfNew(std::string key);
    void Clear() noexcept;
private:
    std::list<std::string> lru_;
    std::unordered_map<std::string, std::list<std::string>::iterator> entries_;
};

class TokenClusterAggregator final {
public:
    explicit TokenClusterAggregator(MonitoringSettings settings);
    [[nodiscard]] std::optional<FrozenTokenCluster> Add(const WalletBuyEvent& event, std::chrono::system_clock::time_point now);
    void Prune(std::chrono::system_clock::time_point now);
    void Clear() noexcept;
private:
    using Events = std::deque<WalletBuyEvent>;
    using WalletEvents = std::unordered_map<std::string, Events>;
    std::optional<FrozenTokenCluster> FreezeIfQualified(const std::string& token, std::chrono::system_clock::time_point now);
    MonitoringSettings settings_;
    std::unordered_map<std::string, WalletEvents> active_;
};

// Thread-affine session controller. A later poller owns its worker and calls this
// controller serially; it deliberately performs neither I/O nor UI dispatch.
class MonitoringController final {
public:
    explicit MonitoringController(IClock& clock);

    [[nodiscard]] bool Start(const AppSettings& settings);
    [[nodiscard]] MonitoringUpdate HandleInitialPage(const FollowWalletPage& page);
    [[nodiscard]] MonitoringUpdate HandleInitialResult(const GmgnResult<FollowWalletPage>& result);
    [[nodiscard]] MonitoringUpdate HandlePollResult(const GmgnResult<FollowWalletPage>& result);
    void Stop() noexcept;
    [[nodiscard]] MonitoringState State() const noexcept;

private:
    [[nodiscard]] MonitoringUpdate ProcessPage(const FollowWalletPage& page, bool initial);
    [[nodiscard]] static MonitoringSettings ToMonitoringSettings(const AppSettings& settings) noexcept;

    IClock& clock_;
    MonitoringState state_{MonitoringState::Stopped};
    std::optional<std::chrono::system_clock::time_point> startedAt_;
    std::optional<EventDeduplicator> deduplicator_;
    std::optional<TokenClusterAggregator> aggregator_;
};

class WalletActivityPoller final {
public:
    using UpdateHandler = std::function<void(const MonitoringUpdate&)>;

    WalletActivityPoller(std::shared_ptr<IGmgnClient> client, MonitoringController& controller, UpdateHandler handler);
    ~WalletActivityPoller();
    WalletActivityPoller(const WalletActivityPoller&) = delete;
    WalletActivityPoller& operator=(const WalletActivityPoller&) = delete;

    [[nodiscard]] bool Start(const AppSettings& settings);
    void Stop() noexcept;
    [[nodiscard]] static std::chrono::milliseconds RetryDelay(std::size_t consecutiveFailures, std::uint32_t jitterBasisPoints = 10'000) noexcept;

private:
    void Run(std::stop_token stop, std::chrono::seconds pollInterval);
    void Publish(const MonitoringUpdate& update) const;

    std::shared_ptr<IGmgnClient> client_;
    MonitoringController& controller_;
    UpdateHandler handler_;
    std::jthread worker_;
    std::condition_variable_any wake_;
    std::uint32_t jitterState_{0x9E3779B9U};
};

// The monitoring worker hands frozen clusters to Phase 04 through this bounded,
// cancellation-aware queue. It stores no historical data and is cleared on stop.
class FrozenClusterQueue final {
public:
    static constexpr std::size_t kCapacity = 32;
    [[nodiscard]] bool TryEnqueue(FrozenTokenCluster cluster);
    [[nodiscard]] std::optional<FrozenTokenCluster> WaitDequeue(std::stop_token stop);
    void Clear() noexcept;

private:
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<FrozenTokenCluster> clusters_;
};

} // namespace gmemmonitor::core
