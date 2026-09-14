#include "gmemmonitor/core/Settings.h"

#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <string_view>
#include <system_error>
#define NOMINMAX
#include <windows.h>

namespace gmemmonitor::core {
namespace {
constexpr std::uintmax_t kMaximumConfigBytes = 4 * 1024;

class FlatJsonReader final {
public:
    explicit FlatJsonReader(const std::string_view text) : text_(text) {}

    [[nodiscard]] std::optional<std::map<std::string, std::uint64_t>> ReadObject() {
        std::map<std::string, std::uint64_t> values;
        SkipWhitespace();
        if (!Take('{')) return std::nullopt;
        SkipWhitespace();
        if (Take('}')) return values;
        while (true) {
            const auto key = ReadString();
            if (!key) return std::nullopt;
            SkipWhitespace();
            if (!Take(':')) return std::nullopt;
            SkipWhitespace();
            const auto value = ReadUnsignedInteger();
            if (!value || !values.emplace(*key, *value).second) return std::nullopt;
            SkipWhitespace();
            if (Take('}')) break;
            if (!Take(',')) return std::nullopt;
            SkipWhitespace();
        }
        SkipWhitespace();
        return position_ == text_.size() ? std::optional{std::move(values)} : std::nullopt;
    }

private:
    void SkipWhitespace() noexcept {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\n' || text_[position_] == '\r' || text_[position_] == '\t')) ++position_;
    }
    [[nodiscard]] bool Take(const char value) noexcept {
        if (position_ >= text_.size() || text_[position_] != value) return false;
        ++position_;
        return true;
    }
    [[nodiscard]] std::optional<std::string> ReadString() {
        if (!Take('"')) return std::nullopt;
        const auto begin = position_;
        while (position_ < text_.size() && text_[position_] != '"') {
            const unsigned char character = static_cast<unsigned char>(text_[position_]);
            if (character < 0x20 || character == '\\') return std::nullopt;
            ++position_;
        }
        if (position_ == text_.size()) return std::nullopt;
        std::string result{text_.substr(begin, position_ - begin)};
        ++position_;
        return result;
    }
    [[nodiscard]] std::optional<std::uint64_t> ReadUnsignedInteger() {
        const auto begin = position_;
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        if (begin == position_) return std::nullopt;
        std::uint64_t value{};
        const auto [end, error] = std::from_chars(text_.data() + begin, text_.data() + position_, value);
        if (error != std::errc{} || end != text_.data() + position_) return std::nullopt;
        return value;
    }

    std::string_view text_;
    std::size_t position_{};
};

[[nodiscard]] std::optional<AppSettings> ParseSettings(const std::string_view text) {
    const auto values = FlatJsonReader(text).ReadObject();
    if (!values || values->size() != 6) return std::nullopt;
    const auto schema = values->find("schema_version");
    const auto poll = values->find("poll_interval_seconds");
    const auto minimum = values->find("minimum_buy_usd_micros");
    const auto wallets = values->find("required_distinct_wallets");
    const auto window = values->find("aggregation_window_seconds");
    const auto cooldown = values->find("notification_cooldown_seconds");
    if (schema == values->end() || poll == values->end() || minimum == values->end() || wallets == values->end() || window == values->end() || cooldown == values->end() || schema->second != AppSettings::kSchemaVersion) return std::nullopt;
    if (poll->second > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || minimum->second > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || wallets->second > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) || window->second > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) || cooldown->second > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return std::nullopt;
    AppSettings settings{
        std::chrono::seconds{static_cast<std::int64_t>(poll->second)},
        {static_cast<std::int64_t>(minimum->second)},
        static_cast<std::size_t>(wallets->second),
        std::chrono::seconds{static_cast<std::int64_t>(window->second)},
        std::chrono::seconds{static_cast<std::int64_t>(cooldown->second)},
    };
    return ValidateSettings(settings) ? std::nullopt : std::optional{settings};
}

[[nodiscard]] std::string SerializeSettings(const AppSettings& settings) {
    return "{\n"
           "  \"schema_version\": 1,\n"
           "  \"poll_interval_seconds\": " + std::to_string(settings.pollInterval.count()) + ",\n"
           "  \"minimum_buy_usd_micros\": " + std::to_string(settings.minimumBuyUsd.micros) + ",\n"
           "  \"required_distinct_wallets\": " + std::to_string(settings.distinctWalletThreshold) + ",\n"
           "  \"aggregation_window_seconds\": " + std::to_string(settings.aggregationWindow.count()) + ",\n"
           "  \"notification_cooldown_seconds\": " + std::to_string(settings.notificationCooldown.count()) + "\n"
           "}\n";
}

[[nodiscard]] std::filesystem::path TemporarySiblingPath(const std::filesystem::path& target) {
    return target.parent_path() / (target.filename().wstring() + L".tmp");
}
} // namespace

std::optional<std::string> ValidateSettings(const AppSettings& settings) noexcept {
    if (settings.pollInterval < std::chrono::seconds{1} || settings.pollInterval > std::chrono::hours{1}) return "Poll interval must be between 1 second and 1 hour.";
    if (settings.minimumBuyUsd.micros <= 0 || settings.minimumBuyUsd.micros > 1'000'000'000'000'000LL) return "Minimum BUY amount is outside the supported range.";
    if (settings.distinctWalletThreshold < 2 || settings.distinctWalletThreshold > 100) return "Required distinct wallets must be between 2 and 100.";
    if (settings.aggregationWindow < std::chrono::seconds{1} || settings.aggregationWindow > std::chrono::hours{1}) return "Aggregation window must be between 1 second and 1 hour.";
    if (settings.notificationCooldown < std::chrono::seconds{1} || settings.notificationCooldown > std::chrono::hours{24}) return "Notification cooldown must be between 1 second and 24 hours.";
    return std::nullopt;
}

SettingsStore::SettingsStore(std::filesystem::path configPath) : configPath_(std::move(configPath)) {}

SettingsLoadResult SettingsStore::Load() const {
    std::error_code error;
    if (!std::filesystem::exists(configPath_, error)) return {};
    if (error || !std::filesystem::is_regular_file(configPath_, error) || error || std::filesystem::file_size(configPath_, error) > kMaximumConfigBytes || error) {
        return {{}, "Saved settings are unavailable or invalid; defaults were loaded."};
    }
    std::ifstream input(configPath_, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(input)), {});
    if (!input.good() && !input.eof()) return {{}, "Saved settings are unavailable or invalid; defaults were loaded."};
    const auto settings = ParseSettings(contents);
    return settings ? SettingsLoadResult{*settings, std::nullopt} : SettingsLoadResult{{}, "Saved settings are invalid; defaults were loaded."};
}

std::optional<std::string> SettingsStore::Save(const AppSettings& settings) const {
    if (const auto validation = ValidateSettings(settings)) return validation;
    std::error_code error;
    std::filesystem::create_directories(configPath_.parent_path(), error);
    if (error) return "Settings directory could not be created.";
    const auto temporary = TemporarySiblingPath(configPath_);
    std::filesystem::remove(temporary, error);
    error.clear();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << SerializeSettings(settings);
        output.flush();
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporary, error);
            return "Settings could not be saved.";
        }
    }
    if (!::MoveFileExW(temporary.c_str(), configPath_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        return "Settings could not be saved.";
    }
    return std::nullopt;
}

} // namespace gmemmonitor::core
