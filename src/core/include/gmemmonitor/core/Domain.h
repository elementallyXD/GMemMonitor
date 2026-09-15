#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <chrono>
#include <vector>

namespace gmemmonitor::core {

struct EvmAddress final {
    std::array<std::byte, 20> bytes{};

    [[nodiscard]] static std::optional<EvmAddress> Parse(std::string_view text) noexcept;
    [[nodiscard]] std::string ToCanonicalString() const;
    friend bool operator==(const EvmAddress&, const EvmAddress&) = default;
};

struct MoneyUsd final {
    std::int64_t micros{};

    [[nodiscard]] static std::optional<MoneyUsd> Parse(std::string_view decimal) noexcept;
};

enum class Chain { Bsc };
enum class TradeSide { Buy };

struct WalletBuyEvent final {
    std::string stableKey;
    std::string gmgnRecordId;
    EvmAddress wallet;
    EvmAddress token;
    std::string sanitizedSymbol;
    MoneyUsd amountUsd;
    std::string baseAmount;
    std::string priceUsd;
    std::chrono::system_clock::time_point timestamp;
    Chain chain{Chain::Bsc};
    TradeSide side{TradeSide::Buy};
    std::string transactionHash;
};

struct WalletContribution final { EvmAddress wallet; WalletBuyEvent largestUnexpiredBuy; };
struct FrozenTokenCluster final { EvmAddress token; std::vector<WalletContribution> wallets; std::chrono::system_clock::time_point triggeredAt; };

[[nodiscard]] std::string SanitizeDisplayText(std::string_view value, std::size_t maximumBytes = 64);
[[nodiscard]] std::string BuildFallbackEventKey(const WalletBuyEvent& event);

} // namespace gmemmonitor::core
