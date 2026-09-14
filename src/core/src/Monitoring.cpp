#include "gmemmonitor/core/Monitoring.h"

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

} // namespace gmemmonitor::core
