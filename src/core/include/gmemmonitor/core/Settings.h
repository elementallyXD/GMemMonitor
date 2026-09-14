#pragma once

#include "gmemmonitor/core/Domain.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace gmemmonitor::core {

// This intentionally contains only local, non-secret monitoring preferences.
struct AppSettings final {
    static constexpr std::uint64_t kSchemaVersion = 1;

    std::chrono::seconds pollInterval{20};
    MoneyUsd minimumBuyUsd{100'000'000};
    std::size_t distinctWalletThreshold{5};
    std::chrono::seconds aggregationWindow{60};
    std::chrono::seconds notificationCooldown{600};
};

[[nodiscard]] std::optional<std::string> ValidateSettings(const AppSettings& settings) noexcept;

struct SettingsLoadResult final {
    AppSettings settings{};
    std::optional<std::string> warning;
};

class SettingsStore final {
public:
    explicit SettingsStore(std::filesystem::path configPath);

    // A missing file is normal. Invalid files are never rewritten by Load().
    [[nodiscard]] SettingsLoadResult Load() const;
    // Returns a short, non-sensitive error suitable for a recoverable UI message.
    [[nodiscard]] std::optional<std::string> Save(const AppSettings& settings) const;

private:
    std::filesystem::path configPath_;
};

} // namespace gmemmonitor::core
