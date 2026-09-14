#pragma once

#include "gmemmonitor/core/Domain.h"
#include "gmemmonitor/core/Process.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <variant>

namespace gmemmonitor::core {

enum class GmgnFailureCode { Authentication, RateLimited, Timeout, Cancelled, ExecutableMissingOrInvalid, ProcessStart, NonZeroExit, OutputLimitExceeded, MalformedJson, UnsupportedSchema, TransientNetworkOrServer };

struct GmgnFailure final { GmgnFailureCode code{}; std::string diagnostic; };
struct FollowWalletPage final {
    std::vector<WalletBuyEvent> events;
    std::optional<std::string> nextPageToken;
    std::size_t rejectedRecordCount{};
};
struct TokenInfo final { EvmAddress token; std::string sanitizedSymbol; std::string gmgnLink; std::optional<std::string> lockedRatio; };
struct TokenSecurity final { EvmAddress token; std::optional<bool> honeypot; std::optional<bool> openSource; std::optional<bool> renounced; std::optional<std::string> buyTax; std::optional<std::string> sellTax; };

template <typename T>
using GmgnResult = std::variant<T, GmgnFailure>;

[[nodiscard]] GmgnResult<FollowWalletPage> ParseFollowWalletPageJson(std::string_view json);
[[nodiscard]] GmgnResult<TokenInfo> ParseTokenInfoJson(std::string_view json);
[[nodiscard]] GmgnResult<TokenSecurity> ParseTokenSecurityJson(std::string_view json);

struct IGmgnClient {
    virtual ~IGmgnClient() = default;
    virtual GmgnResult<FollowWalletPage> FetchFollowWalletBuys(std::stop_token stop) = 0;
    virtual GmgnResult<TokenInfo> FetchTokenInfo(const EvmAddress& token, std::stop_token stop) = 0;
    virtual GmgnResult<TokenSecurity> FetchTokenSecurity(const EvmAddress& token, std::stop_token stop) = 0;
};

struct GmgnRuntimePaths final { std::filesystem::path nodeExecutable; std::filesystem::path cliEntry; };

class GmgnCliClient final : public IGmgnClient {
public:
    GmgnCliClient(GmgnRuntimePaths runtime, std::shared_ptr<IProcessRunner> processRunner);
    GmgnResult<FollowWalletPage> FetchFollowWalletBuys(std::stop_token stop) override;
    GmgnResult<TokenInfo> FetchTokenInfo(const EvmAddress& token, std::stop_token stop) override;
    GmgnResult<TokenSecurity> FetchTokenSecurity(const EvmAddress& token, std::stop_token stop) override;

private:
    GmgnResult<std::string> RunAndReadJson(const std::vector<std::wstring>& command, std::stop_token stop) const;
    GmgnRuntimePaths runtime_;
    std::shared_ptr<IProcessRunner> processRunner_;
    mutable std::mutex rateLimitMutex_;
    mutable std::chrono::steady_clock::time_point rateLimitBlockedUntil_{};
};

} // namespace gmemmonitor::core
