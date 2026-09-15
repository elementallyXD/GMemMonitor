#include "gmemmonitor/core/Domain.h"

#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>

#pragma comment(lib, "bcrypt.lib")

namespace gmemmonitor::core {
namespace {

[[nodiscard]] constexpr int HexValue(const char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

[[nodiscard]] std::string CanonicalDecimal(std::string_view value) {
    const auto decimal = value.find('.');
    const auto wholeEnd = decimal == std::string_view::npos ? value.size() : decimal;
    std::string result;
    if (wholeEnd == 0) {
        result = "0";
    } else {
        auto wholeBegin = value.find_first_not_of('0');
        if (wholeBegin == std::string_view::npos || wholeBegin >= wholeEnd) wholeBegin = wholeEnd - 1;
        result.assign(value.substr(wholeBegin, wholeEnd - wholeBegin));
    }
    if (decimal != std::string_view::npos) {
        auto fractionEnd = value.size();
        while (fractionEnd > decimal + 1 && value[fractionEnd - 1] == '0') --fractionEnd;
        if (fractionEnd > decimal + 1) result.append(value.substr(decimal, fractionEnd - decimal));
    }
    return result;
}

[[nodiscard]] std::string LowerAscii(std::string value) {
    for (char& character : value) if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    return value;
}

[[nodiscard]] bool IsBidiFormattingCharacter(const std::uint32_t codePoint) noexcept {
    return codePoint == 0x061CU || codePoint == 0x200EU || codePoint == 0x200FU ||
        (codePoint >= 0x202AU && codePoint <= 0x202EU) ||
        (codePoint >= 0x2066U && codePoint <= 0x2069U);
}

[[nodiscard]] bool DecodeUtf8(const std::string_view value, const std::size_t offset,
    std::uint32_t& codePoint, std::size_t& byteCount) noexcept {
    const auto first = static_cast<unsigned char>(value[offset]);
    if (first < 0x80U) {
        codePoint = first;
        byteCount = 1;
        return true;
    }

    std::size_t expected{};
    std::uint32_t decoded{};
    std::uint32_t minimum{};
    if ((first & 0xE0U) == 0xC0U) { expected = 2; decoded = first & 0x1FU; minimum = 0x80U; }
    else if ((first & 0xF0U) == 0xE0U) { expected = 3; decoded = first & 0x0FU; minimum = 0x800U; }
    else if ((first & 0xF8U) == 0xF0U) { expected = 4; decoded = first & 0x07U; minimum = 0x10000U; }
    else return false;
    if (offset + expected > value.size()) return false;

    for (std::size_t index = 1; index < expected; ++index) {
        const auto next = static_cast<unsigned char>(value[offset + index]);
        if ((next & 0xC0U) != 0x80U) return false;
        decoded = (decoded << 6U) | (next & 0x3FU);
    }
    if (decoded < minimum || decoded > 0x10FFFFU || (decoded >= 0xD800U && decoded <= 0xDFFFU)) return false;
    codePoint = decoded;
    byteCount = expected;
    return true;
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
    for (std::size_t offset{}; offset < value.size();) {
        std::uint32_t codePoint{};
        std::size_t byteCount{};
        if (!DecodeUtf8(value, offset, codePoint, byteCount)) {
            ++offset;
            continue;
        }
        offset += byteCount;
        if (codePoint < 0x20U || (codePoint >= 0x7FU && codePoint <= 0x9FU) || IsBidiFormattingCharacter(codePoint)) continue;
        if (sanitized.size() + byteCount > maximumBytes) break;
        sanitized.append(value.substr(offset - byteCount, byteCount));
    }
    return sanitized;
}

std::string BuildFallbackEventKey(const WalletBuyEvent& event) {
    const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(event.timestamp.time_since_epoch()).count();
    std::string payload;
    const auto appendField = [&payload](const std::string& value) { payload.append(value); payload.push_back('\0'); };
    appendField("bsc");
    appendField(LowerAscii(event.transactionHash));
    appendField(event.wallet.ToCanonicalString());
    appendField(event.token.ToCanonicalString());
    appendField("buy");
    appendField(std::to_string(timestamp));
    appendField(CanonicalDecimal(event.baseAmount));
    payload.append(std::to_string(event.amountUsd.micros));
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    DWORD objectBytes{};
    DWORD received{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &received, 0) != 0) {
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    std::vector<std::byte> object(objectBytes);
    std::array<std::byte, 32> digest{};
    const NTSTATUS created = BCryptCreateHash(algorithm, &hash, reinterpret_cast<PUCHAR>(object.data()), objectBytes, nullptr, 0, 0);
    const NTSTATUS hashed = created == 0 ? BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(payload.data())), static_cast<ULONG>(payload.size()), 0) : created;
    const NTSTATUS finished = hashed == 0 ? BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()), 0) : hashed;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (finished != 0) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string key{"fallback:"};
    key.reserve(key.size() + digest.size() * 2);
    for (const auto byte : digest) { const auto value = std::to_integer<unsigned char>(byte); key.push_back(hex[value >> 4]); key.push_back(hex[value & 0x0F]); }
    return key;
}

} // namespace gmemmonitor::core
