#pragma once

#include "gmemmonitor/core/Domain.h"

#include <chrono>
#include <deque>
#include <list>
#include <optional>
#include <unordered_map>

namespace gmemmonitor::core {

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

} // namespace gmemmonitor::core
