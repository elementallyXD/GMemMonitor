#include "gmemmonitor/core/GmgnClient.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <string_view>
#include <utility>

namespace gmemmonitor::core {
namespace {

constexpr std::size_t kMaximumJsonDepth = 64;
constexpr std::size_t kMaximumScalarBytes = 1024;

struct JsonValue final {
    enum class Type { Null, Boolean, String, Number, Array, Object } type{Type::Null};
    std::string scalar;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;
};

class JsonParser final {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    [[nodiscard]] bool Parse(JsonValue* result) {
        SkipWhitespace();
        return ParseValue(result, 0) && (SkipWhitespace(), position_ == input_.size());
    }

private:
    [[nodiscard]] bool ParseValue(JsonValue* result, const std::size_t depth) {
        if (depth > kMaximumJsonDepth || position_ == input_.size()) return false;
        switch (input_[position_]) {
        case '{': return ParseObject(result, depth + 1);
        case '[': return ParseArray(result, depth + 1);
        case '"':
            result->type = JsonValue::Type::String;
            return ParseString(&result->scalar);
        case 't': return ConsumeLiteral("true", result, JsonValue::Type::Boolean);
        case 'f': return ConsumeLiteral("false", result, JsonValue::Type::Boolean);
        case 'n': return ConsumeLiteral("null", result, JsonValue::Type::Null);
        default:
            result->type = JsonValue::Type::Number;
            return ParseNumber(&result->scalar);
        }
    }

    [[nodiscard]] bool ParseObject(JsonValue* result, const std::size_t depth) {
        ++position_;
        result->type = JsonValue::Type::Object;
        SkipWhitespace();
        if (Consume('}')) return true;
        while (true) {
            std::string name;
            JsonValue value;
            if (!ParseString(&name)) return false;
            SkipWhitespace();
            if (!Consume(':')) return false;
            SkipWhitespace();
            if (!ParseValue(&value, depth)) return false;
            result->object.emplace_back(std::move(name), std::move(value));
            SkipWhitespace();
            if (Consume('}')) return true;
            if (!Consume(',')) return false;
            SkipWhitespace();
        }
    }

    [[nodiscard]] bool ParseArray(JsonValue* result, const std::size_t depth) {
        ++position_;
        result->type = JsonValue::Type::Array;
        SkipWhitespace();
        if (Consume(']')) return true;
        while (true) {
            JsonValue value;
            if (!ParseValue(&value, depth)) return false;
            result->array.emplace_back(std::move(value));
            SkipWhitespace();
            if (Consume(']')) return true;
            if (!Consume(',')) return false;
            SkipWhitespace();
        }
    }

    [[nodiscard]] bool ParseString(std::string* output) {
        if (!Consume('"')) return false;
        output->clear();
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') return output->size() <= kMaximumScalarBytes;
            if (character < 0x20U || output->size() > kMaximumScalarBytes) return false;
            if (character != '\\') {
                output->push_back(static_cast<char>(character));
                continue;
            }
            if (position_ == input_.size()) return false;
            const char escape = input_[position_++];
            switch (escape) {
            case '"': output->push_back('"'); break;
            case '\\': output->push_back('\\'); break;
            case '/': output->push_back('/'); break;
            case 'b': output->push_back('\b'); break;
            case 'f': output->push_back('\f'); break;
            case 'n': output->push_back('\n'); break;
            case 'r': output->push_back('\r'); break;
            case 't': output->push_back('\t'); break;
            case 'u':
                if (!AppendUnicodeEscape(output)) return false;
                break;
            default: return false;
            }
        }
        return false;
    }

    [[nodiscard]] bool AppendUnicodeEscape(std::string* output) {
        const auto first = ParseHexCodeUnit();
        if (!first) return false;
        std::uint32_t codePoint = *first;
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
            if (position_ + 2 > input_.size() || input_[position_] != '\\' || input_[position_ + 1] != 'u') return false;
            position_ += 2;
            const auto second = ParseHexCodeUnit();
            if (!second || *second < 0xDC00 || *second > 0xDFFF) return false;
            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (*second - 0xDC00);
        } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
            return false;
        }
        return AppendUtf8(codePoint, output);
    }

    [[nodiscard]] std::optional<std::uint32_t> ParseHexCodeUnit() {
        if (position_ + 4 > input_.size()) return std::nullopt;
        std::uint32_t value{};
        for (int index = 0; index < 4; ++index) {
            const char character = input_[position_++];
            const int digit = character >= '0' && character <= '9' ? character - '0' :
                character >= 'a' && character <= 'f' ? character - 'a' + 10 :
                character >= 'A' && character <= 'F' ? character - 'A' + 10 : -1;
            if (digit < 0) return std::nullopt;
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }
        return value;
    }

    [[nodiscard]] static bool AppendUtf8(const std::uint32_t value, std::string* output) {
        if (value <= 0x7F) output->push_back(static_cast<char>(value));
        else if (value <= 0x7FF) {
            output->push_back(static_cast<char>(0xC0 | (value >> 6)));
            output->push_back(static_cast<char>(0x80 | (value & 0x3F)));
        } else if (value <= 0xFFFF) {
            output->push_back(static_cast<char>(0xE0 | (value >> 12)));
            output->push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
            output->push_back(static_cast<char>(0x80 | (value & 0x3F)));
        } else if (value <= 0x10FFFF) {
            output->push_back(static_cast<char>(0xF0 | (value >> 18)));
            output->push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
            output->push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
            output->push_back(static_cast<char>(0x80 | (value & 0x3F)));
        } else return false;
        return output->size() <= kMaximumScalarBytes;
    }

    [[nodiscard]] bool ParseNumber(std::string* output) {
        const std::size_t start = position_;
        static_cast<void>(Consume('-'));
        if (Consume('0')) {
        } else {
            if (!ConsumeDigit('1', '9')) return false;
            while (position_ < input_.size() && IsDigit(input_[position_])) ++position_;
        }
        if (Consume('.')) {
            if (position_ == input_.size() || !IsDigit(input_[position_])) return false;
            while (position_ < input_.size() && IsDigit(input_[position_])) ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            if (position_ == input_.size() || !IsDigit(input_[position_])) return false;
            while (position_ < input_.size() && IsDigit(input_[position_])) ++position_;
        }
        if (position_ - start > kMaximumScalarBytes) return false;
        output->assign(input_.substr(start, position_ - start));
        return true;
    }

    [[nodiscard]] bool ConsumeLiteral(const std::string_view literal, JsonValue* result, const JsonValue::Type type) {
        if (input_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        result->type = type;
        result->scalar.assign(literal);
        return true;
    }

    [[nodiscard]] bool Consume(const char value) {
        if (position_ == input_.size() || input_[position_] != value) return false;
        ++position_;
        return true;
    }

    [[nodiscard]] bool ConsumeDigit(const char first, const char last) {
        if (position_ == input_.size() || input_[position_] < first || input_[position_] > last) return false;
        ++position_;
        return true;
    }

    static bool IsDigit(const char value) { return value >= '0' && value <= '9'; }

    void SkipWhitespace() {
        while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\n' ||
            input_[position_] == '\r' || input_[position_] == '\t')) ++position_;
    }

    std::string_view input_;
    std::size_t position_{};
};

[[nodiscard]] const JsonValue* FindUnique(const JsonValue& object, const std::string_view name) {
    const JsonValue* result = nullptr;
    for (const auto& [candidate, value] : object.object) {
        if (candidate == name) {
            if (result != nullptr) return nullptr;
            result = &value;
        }
    }
    return result;
}

[[nodiscard]] bool HasDuplicate(const JsonValue& object, const std::string_view name) {
    std::size_t count{};
    for (const auto& [candidate, ignored] : object.object) {
        static_cast<void>(ignored);
        if (candidate == name && ++count > 1) return true;
    }
    return false;
}

[[nodiscard]] bool StringOrNumber(const JsonValue* value, std::string* result) {
    if (!value || (value->type != JsonValue::Type::String && value->type != JsonValue::Type::Number) || value->scalar.empty()) return false;
    *result = value->scalar;
    return true;
}

[[nodiscard]] bool StrictString(const JsonValue* value, std::string* result) {
    if (!value || value->type != JsonValue::Type::String || value->scalar.empty()) return false;
    *result = value->scalar;
    return true;
}

[[nodiscard]] bool IsAsciiEqualInsensitive(const std::string_view left, const std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) != std::tolower(static_cast<unsigned char>(right[index]))) return false;
    }
    return true;
}

[[nodiscard]] bool IsValidTransactionHash(const std::string_view value) {
    if (value.size() != 66 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X')) return false;
    return std::all_of(value.begin() + 2, value.end(), [](const char character) {
        return std::isxdigit(static_cast<unsigned char>(character)) != 0;
    });
}

[[nodiscard]] bool ParseTimestamp(const JsonValue* value, std::chrono::system_clock::time_point* output) {
    std::string text;
    if (!StringOrNumber(value, &text) || text.find_first_of(".eE+") != std::string::npos) return false;
    std::int64_t seconds{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seconds);
    if (error != std::errc{} || end != text.data() + text.size() || seconds < 0) return false;
    *output = std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
    return true;
}

[[nodiscard]] bool IsPlainDecimal(const std::string_view value) {
    if (value.empty() || value.size() > kMaximumScalarBytes) return false;
    bool hasDigits = false;
    bool hasDecimalPoint = false;
    for (const char character : value) {
        if (character >= '0' && character <= '9') {
            hasDigits = true;
        } else if (character == '.' && !hasDecimalPoint) {
            hasDecimalPoint = true;
        } else {
            return false;
        }
    }
    return hasDigits;
}

[[nodiscard]] bool ParseRecord(const JsonValue& record, WalletBuyEvent* event) {
    if (record.type != JsonValue::Type::Object) return false;
    std::string chain;
    std::string side;
    std::string transactionHash;
    std::string maker;
    std::string token;
    std::string amountUsd;
    std::string baseAmount;
    std::string priceUsd;
    if (HasDuplicate(record, "id") || !StrictString(FindUnique(record, "chain"), &chain) || !IsAsciiEqualInsensitive(chain, "bsc") ||
        !StrictString(FindUnique(record, "side"), &side) || !IsAsciiEqualInsensitive(side, "buy") ||
        !StrictString(FindUnique(record, "transaction_hash"), &transactionHash) || !IsValidTransactionHash(transactionHash) ||
        !StrictString(FindUnique(record, "maker"), &maker) || !StrictString(FindUnique(record, "base_address"), &token) ||
        !StringOrNumber(FindUnique(record, "amount_usd"), &amountUsd) ||
        !StringOrNumber(FindUnique(record, "base_amount"), &baseAmount) ||
        !StringOrNumber(FindUnique(record, "price_usd"), &priceUsd)) return false;

    const auto wallet = EvmAddress::Parse(maker);
    const auto tokenAddress = EvmAddress::Parse(token);
    const auto money = MoneyUsd::Parse(amountUsd);
    if (!wallet || !tokenAddress || !money || !IsPlainDecimal(priceUsd) || !IsPlainDecimal(baseAmount)) return false;

    WalletBuyEvent parsed;
    if (const JsonValue* id = FindUnique(record, "id")) {
        if (id->type != JsonValue::Type::String || id->scalar.size() > 512) return false;
        parsed.gmgnRecordId = id->scalar;
        if (!parsed.gmgnRecordId.empty()) parsed.stableKey = "gmgn:" + parsed.gmgnRecordId;
    }
    if (!ParseTimestamp(FindUnique(record, "timestamp"), &parsed.timestamp)) return false;

    parsed.wallet = *wallet;
    parsed.token = *tokenAddress;
    parsed.amountUsd = *money;
    parsed.baseAmount = std::move(baseAmount);
    parsed.priceUsd = std::move(priceUsd);
    parsed.transactionHash = std::move(transactionHash);
    if (parsed.stableKey.empty()) {
        parsed.stableKey = BuildFallbackEventKey(parsed);
        if (parsed.stableKey.empty()) return false;
    }
    if (const JsonValue* baseToken = FindUnique(record, "base_token"); baseToken && baseToken->type == JsonValue::Type::Object) {
        std::string symbol;
        if (const JsonValue* value = FindUnique(*baseToken, "symbol"); value && StrictString(value, &symbol)) {
            parsed.sanitizedSymbol = SanitizeDisplayText(symbol);
        }
    }
    *event = std::move(parsed);
    return true;
}

[[nodiscard]] GmgnFailure SchemaFailure() {
    return {GmgnFailureCode::UnsupportedSchema, "GMGN follow-wallet response did not match the captured contract."};
}

} // namespace

GmgnResult<FollowWalletPage> ParseFollowWalletPageJson(const std::string_view json) {
    JsonValue root;
    JsonParser parser(json);
    if (!parser.Parse(&root)) {
        return GmgnFailure{GmgnFailureCode::MalformedJson, "GMGN follow-wallet response was not valid JSON."};
    }
    if (root.type != JsonValue::Type::Object) return SchemaFailure();
    const JsonValue* list = FindUnique(root, "list");
    if (!list || list->type != JsonValue::Type::Array) return SchemaFailure();

    FollowWalletPage page;
    if (const JsonValue* token = FindUnique(root, "next_page_token")) {
        if (token->type == JsonValue::Type::String && !token->scalar.empty()) page.nextPageToken = token->scalar;
        else if (token->type != JsonValue::Type::Null) return SchemaFailure();
    }
    for (const JsonValue& record : list->array) {
        WalletBuyEvent event;
        if (!ParseRecord(record, &event)) {
            ++page.rejectedRecordCount;
            continue;
        }
        page.events.emplace_back(std::move(event));
    }
    return page;
}

namespace {
[[nodiscard]] const JsonValue* ParseRootObject(const std::string_view json, JsonValue* root, GmgnFailure* failure) {
    JsonParser parser(json);
    if (!parser.Parse(root)) {
        *failure = {GmgnFailureCode::MalformedJson, "GMGN response was not valid JSON."};
        return nullptr;
    }
    if (root->type != JsonValue::Type::Object) {
        *failure = {GmgnFailureCode::UnsupportedSchema, "GMGN response did not contain an object root."};
        return nullptr;
    }
    return root;
}

[[nodiscard]] std::optional<bool> OptionalBool(const JsonValue* value, bool* valid) {
    if (!value || value->type == JsonValue::Type::Null) return std::nullopt;
    if (value->type != JsonValue::Type::Boolean) { *valid = false; return std::nullopt; }
    return value->scalar == "true";
}

[[nodiscard]] std::optional<std::string> OptionalDecimal(const JsonValue* value, bool* valid) {
    if (!value || value->type == JsonValue::Type::Null) return std::nullopt;
    std::string decimal;
    if (!StringOrNumber(value, &decimal) || !IsPlainDecimal(decimal)) { *valid = false; return std::nullopt; }
    return decimal;
}

[[nodiscard]] std::optional<std::string> OptionalDisplayValue(const JsonValue* value, bool* valid) {
    if (!value || value->type == JsonValue::Type::Null) return std::nullopt;
    std::string text;
    if (!StringOrNumber(value, &text)) { *valid = false; return std::nullopt; }
    text = SanitizeDisplayText(text);
    return text.empty() ? std::nullopt : std::optional<std::string>{std::move(text)};
}
} // namespace

GmgnResult<TokenInfo> ParseTokenInfoJson(const std::string_view json) {
    JsonValue root;
    GmgnFailure failure;
    if (!ParseRootObject(json, &root, &failure)) return failure;
    std::string address;
    std::string symbol;
    if (!StrictString(FindUnique(root, "address"), &address) || !StrictString(FindUnique(root, "symbol"), &symbol)) return SchemaFailure();
    const auto token = EvmAddress::Parse(address);
    if (!token) return SchemaFailure();
    TokenInfo info{*token, SanitizeDisplayText(symbol), {}, std::nullopt};
    if (const JsonValue* link = FindUnique(root, "link"); link && link->type == JsonValue::Type::Object) {
        const JsonValue* gmgn = FindUnique(*link, "gmgn");
        if (gmgn && gmgn->type != JsonValue::Type::Null && !StrictString(gmgn, &info.gmgnLink)) return SchemaFailure();
    }
    bool valid = true;
    info.lockedRatio = OptionalDecimal(FindUnique(root, "locked_ratio"), &valid);
    return valid ? GmgnResult<TokenInfo>{std::move(info)} : GmgnResult<TokenInfo>{SchemaFailure()};
}

GmgnResult<TokenSecurity> ParseTokenSecurityJson(const std::string_view json) {
    JsonValue root;
    GmgnFailure failure;
    if (!ParseRootObject(json, &root, &failure)) return failure;
    std::string address;
    if (!StrictString(FindUnique(root, "address"), &address)) return SchemaFailure();
    const auto token = EvmAddress::Parse(address);
    if (!token) return SchemaFailure();
    bool valid = true;
    TokenSecurity security{*token};
    security.honeypot = OptionalBool(FindUnique(root, "is_honeypot"), &valid);
    security.openSource = OptionalBool(FindUnique(root, "is_open_source"), &valid);
    security.renounced = OptionalBool(FindUnique(root, "is_renounced"), &valid);
    security.buyTax = OptionalDisplayValue(FindUnique(root, "buy_tax"), &valid);
    security.sellTax = OptionalDisplayValue(FindUnique(root, "sell_tax"), &valid);
    return valid ? GmgnResult<TokenSecurity>{std::move(security)} : GmgnResult<TokenSecurity>{SchemaFailure()};
}

} // namespace gmemmonitor::core
