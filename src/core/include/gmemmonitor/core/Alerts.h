#pragma once

#include "gmemmonitor/core/Domain.h"

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>

namespace gmemmonitor::core {

struct RiskFact final { std::string label; std::string value; bool available{}; };
struct EnrichmentFacts final {
    std::optional<bool> honeypot;
    std::optional<bool> verified;
    std::optional<bool> renounced;
    std::optional<std::string> buyTax;
    std::optional<std::string> sellTax;
    std::string gmgnLink;
};
struct TokenAlert final {
    EvmAddress token;
    std::string sanitizedSymbol;
    std::size_t qualifyingWallets{};
    MoneyUsd largestQualifyingBuy;
    std::vector<RiskFact> risks;
    std::string validatedGmgnUrl;
    std::string title;
    std::string body;
};

[[nodiscard]] std::optional<std::string> ValidateGmgnUrl(std::string_view value);
[[nodiscard]] TokenAlert ComposeAlert(const FrozenTokenCluster& cluster, std::string_view symbol, const EnrichmentFacts& facts);

class CooldownManager final {
public:
    explicit CooldownManager(std::chrono::seconds duration = std::chrono::seconds(600));
    [[nodiscard]] bool IsActive(const EvmAddress& token, std::chrono::steady_clock::time_point now) const;
    void MarkDelivered(const EvmAddress& token, std::chrono::steady_clock::time_point now);
    void Clear() noexcept;
private:
    std::chrono::seconds duration_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> delivered_;
};

} // namespace gmemmonitor::core
