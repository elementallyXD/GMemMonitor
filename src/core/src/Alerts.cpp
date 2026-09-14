#include "gmemmonitor/core/Alerts.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace gmemmonitor::core {
namespace {
[[nodiscard]] std::string Usd(const MoneyUsd money) {
    const auto absolute = money.micros < 0 ? static_cast<std::uint64_t>(-(money.micros + 1)) + 1U : static_cast<std::uint64_t>(money.micros);
    std::ostringstream stream; if (money.micros < 0) stream << '-'; stream << '$' << absolute / 1'000'000U << '.' << std::setw(2) << std::setfill('0') << (absolute % 1'000'000U) / 10'000U; return stream.str();
}
[[nodiscard]] RiskFact BoolFact(std::string label, const std::optional<bool>& value) {
    return {std::move(label), value ? (*value ? "Yes" : "No") : "unavailable", value.has_value()};
}
}

std::optional<std::string> ValidateGmgnUrl(const std::string_view value) {
    constexpr std::string_view prefix{"https://gmgn.ai"};
    if (value.size() < prefix.size()) return std::nullopt;
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (static_cast<char>(std::tolower(static_cast<unsigned char>(value[index]))) != prefix[index]) return std::nullopt;
    }
    if (value.size() == prefix.size()) return std::string(value);
    const char separator = value[prefix.size()];
    if (separator != '/' && separator != '?') return std::nullopt;
    if (value.find('@') != std::string_view::npos || value.find('\\') != std::string_view::npos || value.find_first_of("\r\n\0") != std::string_view::npos) return std::nullopt;
    return std::string(value);
}

TokenAlert ComposeAlert(const FrozenTokenCluster& cluster, const std::string_view symbol, const EnrichmentFacts& facts) {
    TokenAlert alert; alert.token = cluster.token; alert.sanitizedSymbol = SanitizeDisplayText(symbol);
    if (alert.sanitizedSymbol.empty()) alert.sanitizedSymbol = cluster.token.ToCanonicalString().substr(0, 10) + "…";
    alert.qualifyingWallets = cluster.wallets.size();
    for (const auto& wallet : cluster.wallets) if (wallet.largestUnexpiredBuy.amountUsd.micros > alert.largestQualifyingBuy.micros) alert.largestQualifyingBuy = wallet.largestUnexpiredBuy.amountUsd;
    alert.risks = {BoolFact("GMGN reports honeypot", facts.honeypot), BoolFact("Contract source verified", facts.verified), BoolFact("Ownership renounced", facts.renounced), {"Buy tax", facts.buyTax.value_or("unavailable"), facts.buyTax.has_value()}, {"Sell tax", facts.sellTax.value_or("unavailable"), facts.sellTax.has_value()}};
    if (const auto link = ValidateGmgnUrl(facts.gmgnLink)) alert.validatedGmgnUrl = *link;
    alert.title = alert.sanitizedSymbol + " — coordinated BUY activity";
    alert.body = std::to_string(alert.qualifyingWallets) + " followed wallets bought; largest qualifying BUY " + Usd(alert.largestQualifyingBuy);
    return alert;
}

CooldownManager::CooldownManager(const std::chrono::seconds duration) : duration_(duration) {}
bool CooldownManager::IsActive(const EvmAddress& token, const std::chrono::steady_clock::time_point now) const {
    const auto found = delivered_.find(token.ToCanonicalString()); return found != delivered_.end() && now < found->second + duration_;
}
void CooldownManager::MarkDelivered(const EvmAddress& token, const std::chrono::steady_clock::time_point now) { delivered_[token.ToCanonicalString()] = now; }
void CooldownManager::Clear() noexcept { delivered_.clear(); }

} // namespace gmemmonitor::core
