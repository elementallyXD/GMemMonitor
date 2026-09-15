#pragma once

#include "gmemmonitor/core/Alerts.h"
#include "gmemmonitor/core/GmgnClient.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace gmemmonitor::core {

class GmgnRequestScheduler final {
public:
    enum class Priority { Feed, Enrichment };
    class Permit final {
    public:
        Permit() = default; Permit(const Permit&) = delete; Permit& operator=(const Permit&) = delete;
        Permit(Permit&& other) noexcept; Permit& operator=(Permit&& other) noexcept; ~Permit();
    private:
        friend class GmgnRequestScheduler;
        explicit Permit(GmgnRequestScheduler* owner) noexcept : owner_(owner) {}
        GmgnRequestScheduler* owner_{};
    };
    explicit GmgnRequestScheduler(std::size_t maximumActive = 1);
    [[nodiscard]] std::optional<Permit> Acquire(Priority priority, std::stop_token stop);
    [[nodiscard]] std::size_t ActiveCount() const;
    [[nodiscard]] std::size_t PendingCount() const;
    void CancelPending() noexcept;
private:
    struct Waiter final { std::size_t sequence{}; Priority priority{}; };
    void Release() noexcept;
    [[nodiscard]] bool MayRun(std::size_t sequence) const noexcept;
    const std::size_t maximumActive_; mutable std::mutex mutex_; std::condition_variable_any wake_;
    std::vector<Waiter> waiters_; std::size_t active_{}; std::size_t nextSequence_{}; bool cancelled_{};
};

struct IAlertClock { virtual ~IAlertClock() = default; [[nodiscard]] virtual std::chrono::steady_clock::time_point SteadyNow() const = 0; };
struct INotificationService { virtual ~INotificationService() = default; virtual bool Show(const TokenAlert& alert) = 0; };
struct AnalysisUpdate final { bool delivered{}; bool suppressedByCooldown{}; std::string diagnostic; std::optional<TokenAlert> alert; };

class TokenAnalysisService final {
public:
    TokenAnalysisService(std::shared_ptr<IGmgnClient> client, GmgnRequestScheduler& scheduler, INotificationService& notifications, IAlertClock& clock, std::chrono::seconds cooldown = std::chrono::seconds(600));
    [[nodiscard]] AnalysisUpdate Analyze(const FrozenTokenCluster& cluster, std::stop_token stop);
    void ClearSession() noexcept;
private:
    [[nodiscard]] static EnrichmentFacts ToFacts(const TokenInfo& info, const TokenSecurity& security);
    [[nodiscard]] static std::string FailureDiagnostic(const GmgnFailure& failure);
    std::shared_ptr<IGmgnClient> client_; GmgnRequestScheduler& scheduler_; INotificationService& notifications_; IAlertClock& clock_; CooldownManager cooldown_;
};

class TokenAnalysisExecutor final {
public:
    static constexpr std::size_t kCapacity = 32;
    using UpdateHandler = std::function<void(const AnalysisUpdate&)>;
    TokenAnalysisExecutor(TokenAnalysisService& service, UpdateHandler handler = {}); ~TokenAnalysisExecutor();
    TokenAnalysisExecutor(const TokenAnalysisExecutor&) = delete; TokenAnalysisExecutor& operator=(const TokenAnalysisExecutor&) = delete;
    [[nodiscard]] bool Start(); [[nodiscard]] bool TrySubmit(FrozenTokenCluster cluster); void Stop() noexcept;
private:
    void Run(std::stop_token stop);
    TokenAnalysisService& service_; UpdateHandler handler_; std::mutex mutex_; std::condition_variable_any wake_; std::deque<FrozenTokenCluster> pending_; std::jthread worker_;
};

} // namespace gmemmonitor::core
