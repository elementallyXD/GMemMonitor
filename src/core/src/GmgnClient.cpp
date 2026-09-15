#include "gmemmonitor/core/GmgnClient.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string_view>

namespace gmemmonitor::core {
namespace {

constexpr std::chrono::seconds kUnknownRateLimitCooldown{60};
constexpr std::chrono::seconds kMaximumRateLimitCooldown{300};
constexpr std::chrono::seconds kRateLimitSafetyMargin{2};

[[nodiscard]] bool ContainsAsciiInsensitive(const std::string_view text, const std::string_view needle) {
    if (needle.empty() || needle.size() > text.size()) {
        return needle.empty();
    }

    return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
        [](const char left, const char right) {
            return std::tolower(static_cast<unsigned char>(left)) ==
                   std::tolower(static_cast<unsigned char>(right));
        }) != text.end();
}

[[nodiscard]] bool IsRateLimited(const ProcessResult& result) {
    return ContainsAsciiInsensitive(result.stderrOutput.text, "http 429") ||
           ContainsAsciiInsensitive(result.stderrOutput.text, "rate_limit_exceeded") ||
           ContainsAsciiInsensitive(result.stdoutOutput.text, "rate_limit_exceeded");
}

[[nodiscard]] std::optional<std::chrono::seconds> RateLimitDelay(const ProcessResult& result) {
    constexpr std::string_view suffix{"s remaining"};
    const std::string_view text{result.stderrOutput.text};
    const std::size_t suffixPosition = text.find(suffix);
    if (suffixPosition == std::string_view::npos) {
        return std::nullopt;
    }

    std::size_t end = suffixPosition;
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    std::size_t begin = end;
    while (begin > 0 && std::isdigit(static_cast<unsigned char>(text[begin - 1]))) {
        --begin;
    }
    if (begin == end) {
        return std::nullopt;
    }

    long long seconds{};
    const auto [position, error] = std::from_chars(text.data() + begin, text.data() + end, seconds);
    if (error != std::errc{} || position != text.data() + end || seconds < 0) {
        return std::nullopt;
    }
    return std::chrono::seconds{(std::min)(seconds, kMaximumRateLimitCooldown.count())};
}

[[nodiscard]] std::chrono::seconds EffectiveRateLimitCooldown(const ProcessResult& result) {
    const auto reported = RateLimitDelay(result).value_or(kUnknownRateLimitCooldown);
    return (std::min)(reported + kRateLimitSafetyMargin, kMaximumRateLimitCooldown);
}

[[nodiscard]] std::string DiagnosticFor(const ProcessResult& result) {
    if (IsRateLimited(result)) {
        return "GMGN rate limited this public IP. The client will suppress further requests during the cooldown.";
    }
    switch (result.reason) {
    case ProcessTerminationReason::TimedOut: return "GMGN CLI request timed out.";
    case ProcessTerminationReason::Cancelled: return "GMGN CLI request was cancelled.";
    case ProcessTerminationReason::OutputLimitExceeded: return "GMGN CLI output exceeded the configured limit.";
    case ProcessTerminationReason::StartFailed: return "GMGN CLI process could not be started.";
    case ProcessTerminationReason::WaitFailed: return "GMGN CLI process wait failed.";
    case ProcessTerminationReason::Completed: return result.exitCode == 0 ? "GMGN CLI request completed." : "GMGN CLI exited with an error.";
    }
    return "GMGN CLI failed.";
}

[[nodiscard]] GmgnFailureCode CodeFor(const ProcessResult& result) {
    if (IsRateLimited(result)) {
        return GmgnFailureCode::RateLimited;
    }
    switch (result.reason) {
    case ProcessTerminationReason::TimedOut: return GmgnFailureCode::Timeout;
    case ProcessTerminationReason::Cancelled: return GmgnFailureCode::Cancelled;
    case ProcessTerminationReason::OutputLimitExceeded: return GmgnFailureCode::OutputLimitExceeded;
    case ProcessTerminationReason::StartFailed: return GmgnFailureCode::ProcessStart;
    case ProcessTerminationReason::WaitFailed: return GmgnFailureCode::TransientNetworkOrServer;
    case ProcessTerminationReason::Completed: return result.exitCode == 0 ? GmgnFailureCode::MalformedJson : GmgnFailureCode::NonZeroExit;
    }
    return GmgnFailureCode::TransientNetworkOrServer;
}

[[nodiscard]] std::vector<std::wstring> BaseCommand(const GmgnRuntimePaths& runtime) {
    return {runtime.cliEntry.wstring()};
}

} // namespace

GmgnCliClient::GmgnCliClient(GmgnRuntimePaths runtime, std::shared_ptr<IProcessRunner> processRunner)
    : runtime_(std::move(runtime)), processRunner_(std::move(processRunner)) {}

GmgnResult<std::string> GmgnCliClient::RunAndReadJson(const std::vector<std::wstring>& command, std::stop_token stop) const {
    if (!processRunner_ || !runtime_.nodeExecutable.is_absolute() || !runtime_.cliEntry.is_absolute() ||
        !std::filesystem::is_regular_file(runtime_.nodeExecutable) || !std::filesystem::is_regular_file(runtime_.cliEntry)) {
        return GmgnFailure{GmgnFailureCode::ExecutableMissingOrInvalid, "Pinned GMGN runtime files are missing or invalid."};
    }
    {
        std::scoped_lock lock(rateLimitMutex_);
        if (std::chrono::steady_clock::now() < rateLimitBlockedUntil_) {
            return GmgnFailure{GmgnFailureCode::RateLimited,
                "GMGN rate limit cooldown is active. The client did not start another request.",
                std::chrono::duration_cast<std::chrono::seconds>(rateLimitBlockedUntil_ - std::chrono::steady_clock::now()) + std::chrono::seconds{1}};
        }
    }
    ProcessRequest request;
    request.executable = runtime_.nodeExecutable;
    request.workingDirectory = runtime_.nodeExecutable.parent_path();
    request.arguments = command;
    const ProcessResult result = processRunner_->Run(request, stop);
    if (IsRateLimited(result)) {
        std::scoped_lock lock(rateLimitMutex_);
        const auto blockedUntil = std::chrono::steady_clock::now() + EffectiveRateLimitCooldown(result);
        if (blockedUntil > rateLimitBlockedUntil_) {
            rateLimitBlockedUntil_ = blockedUntil;
        }
    }
    if (result.reason != ProcessTerminationReason::Completed || result.exitCode != 0 || result.stdoutOutput.truncated) {
        return GmgnFailure{CodeFor(result), DiagnosticFor(result), IsRateLimited(result) ? std::optional{EffectiveRateLimitCooldown(result)} : std::nullopt};
    }
    return result.stdoutOutput.text;
}

GmgnResult<FollowWalletPage> GmgnCliClient::FetchFollowWalletBuys(std::stop_token stop) {
    auto command = BaseCommand(runtime_);
    command.insert(command.end(), {L"track", L"follow-wallet", L"--chain", L"bsc", L"--side", L"buy", L"--limit", L"100", L"--raw"});
    auto output = RunAndReadJson(command, stop);
    if (const auto* failure = std::get_if<GmgnFailure>(&output)) return *failure;
    return ParseFollowWalletPageJson(std::get<std::string>(std::move(output)));
}

GmgnResult<TokenInfo> GmgnCliClient::FetchTokenInfo(const EvmAddress& token, std::stop_token stop) {
    auto command = BaseCommand(runtime_);
    const std::string address = token.ToCanonicalString();
    command.insert(command.end(), {L"token", L"info", L"--chain", L"bsc", L"--address", std::wstring(address.begin(), address.end()), L"--raw"});
    auto output = RunAndReadJson(command, stop);
    if (const auto* failure = std::get_if<GmgnFailure>(&output)) return *failure;
    auto parsed = ParseTokenInfoJson(std::get<std::string>(std::move(output)));
    if (const auto* info = std::get_if<TokenInfo>(&parsed); info && info->token != token) {
        return GmgnFailure{GmgnFailureCode::UnsupportedSchema, "GMGN token-info response address did not match the requested token."};
    }
    return parsed;
}

GmgnResult<TokenSecurity> GmgnCliClient::FetchTokenSecurity(const EvmAddress& token, std::stop_token stop) {
    auto command = BaseCommand(runtime_);
    const std::string address = token.ToCanonicalString();
    command.insert(command.end(), {L"token", L"security", L"--chain", L"bsc", L"--address", std::wstring(address.begin(), address.end()), L"--raw"});
    auto output = RunAndReadJson(command, stop);
    if (const auto* failure = std::get_if<GmgnFailure>(&output)) return *failure;
    auto parsed = ParseTokenSecurityJson(std::get<std::string>(std::move(output)));
    if (const auto* security = std::get_if<TokenSecurity>(&parsed); security && security->token != token) {
        return GmgnFailure{GmgnFailureCode::UnsupportedSchema, "GMGN token-security response address did not match the requested token."};
    }
    return parsed;
}

} // namespace gmemmonitor::core
