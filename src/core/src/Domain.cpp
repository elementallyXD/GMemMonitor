#include "gmemmonitor/core/Domain.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace gmemmonitor::core {
namespace {

[[nodiscard]] constexpr int HexValue(const char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

} // namespace

std::optional<EvmAddress> EvmAddress::Parse(const std::string_view text) noexcept {
    if (text.size() != 42 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) return std::nullopt;

    EvmAddress address;
    for (std::size_t index = 0; index < address.bytes.size(); ++index) {
        const int high = HexValue(text[2 + index * 2]);
        const int low = HexValue(text[3 + index * 2]);
        if (high < 0 || low < 0) return std::nullopt;
        address.bytes[index] = static_cast<std::byte>((high << 4) | low);
    }
    return address;
}

std::string EvmAddress::ToCanonicalString() const {
    constexpr char digits[] = "0123456789abcdef";
    std::string text{"0x"};
    text.reserve(42);
    for (const auto byte : bytes) {
        const unsigned value = std::to_integer<unsigned char>(byte);
        text.push_back(digits[value >> 4]);
        text.push_back(digits[value & 0x0F]);
    }
    return text;
}

std::optional<MoneyUsd> MoneyUsd::Parse(const std::string_view decimal) noexcept {
    if (decimal.empty()) return std::nullopt;
    std::size_t cursor = 0;
    bool negative = false;
    if (decimal[cursor] == '+' || decimal[cursor] == '-') {
        negative = decimal[cursor++] == '-';
        if (cursor == decimal.size()) return std::nullopt;
    }

    const std::size_t wholeStart = cursor;
    while (cursor < decimal.size() && decimal[cursor] >= '0' && decimal[cursor] <= '9') ++cursor;
    if (cursor == wholeStart) return std::nullopt;

    std::uint64_t whole{};
    const auto [wholeEnd, wholeError] = std::from_chars(decimal.data() + wholeStart, decimal.data() + cursor, whole);
    if (wholeError != std::errc{} || wholeEnd != decimal.data() + cursor) return std::nullopt;

    std::uint64_t fraction{};
    if (cursor < decimal.size()) {
        if (decimal[cursor++] != '.') return std::nullopt;
        std::size_t places{};
        while (cursor < decimal.size()) {
            const char character = decimal[cursor++];
            if (character < '0' || character > '9') return std::nullopt;
            if (places < 6) {
                fraction = fraction * 10U + static_cast<unsigned>(character - '0');
            } else if (character != '0') {
                return std::nullopt;
            }
            ++places;
        }
        while (places++ < 6) fraction *= 10U;
    }

    constexpr std::uint64_t kPositiveLimit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    constexpr std::uint64_t kNegativeLimit = kPositiveLimit + 1U;
    const std::uint64_t limit = negative ? kNegativeLimit : kPositiveLimit;
    if (whole > (limit - fraction) / 1'000'000U) return std::nullopt;
    const std::uint64_t micros = whole * 1'000'000U + fraction;
    if (!negative) return MoneyUsd{static_cast<std::int64_t>(micros)};
    if (micros == kNegativeLimit) return MoneyUsd{std::numeric_limits<std::int64_t>::min()};
    return MoneyUsd{-static_cast<std::int64_t>(micros)};
}

std::string SanitizeDisplayText(const std::string_view value, const std::size_t maximumBytes) {
    std::string sanitized;
    sanitized.reserve((std::min)(value.size(), maximumBytes));
    for (const unsigned char character : value) {
        if (character >= 0x20U && character != 0x7FU) {
            sanitized.push_back(static_cast<char>(character));
            if (sanitized.size() == maximumBytes) break;
        }
    }
    return sanitized;
}

} // namespace gmemmonitor::core
