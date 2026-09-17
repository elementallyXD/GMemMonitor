#include "gmemmonitor/core/Analysis.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gmemmonitor::core {
GmgnRequestScheduler::Permit::Permit(Permit&& other) noexcept : owner_(std::exchange(other.owner_, nullptr)) {}
GmgnRequestScheduler::Permit& GmgnRequestScheduler::Permit::operator=(Permit&& other) noexcept { if (this != &other) { if (owner_) owner_->Release(); owner_ = std::exchange(other.owner_, nullptr); } return *this; }
GmgnRequestScheduler::Permit::~Permit() { if (owner_) owner_->Release(); }
GmgnRequestScheduler::GmgnRequestScheduler(const std::size_t maximumActive,
                                           const std::size_t tokensPerSecond,
                                           const std::size_t tokenCapacity)
    : maximumActive_(maximumActive), tokensPerSecond_(tokensPerSecond),
      tokenCapacity_(tokenCapacity), availableTokens_(static_cast<double>(tokenCapacity)),
      lastRefill_(std::chrono::steady_clock::now()) {
    if (!maximumActive || !tokensPerSecond || !tokenCapacity) {
        throw std::invalid_argument("GMGN scheduler limits must be positive.");
    }
}

void GmgnRequestScheduler::Refill(const std::chrono::steady_clock::time_point now) noexcept {
    const auto elapsed = std::chrono::duration<double>(now - lastRefill_).count();
    if (elapsed <= 0.0) return;
    availableTokens_ = (std::min)(static_cast<double>(tokenCapacity_),
                                  availableTokens_ + elapsed * static_cast<double>(tokensPerSecond_));
    lastRefill_ = now;
}

bool GmgnRequestScheduler::MayRun(const std::size_t sequence) const noexcept {
    if (active_ >= maximumActive_) return false;
    const auto current = std::find_if(waiters_.begin(), waiters_.end(), [sequence](const Waiter& waiter) { return waiter.sequence == sequence; });
    return current != waiters_.end() && availableTokens_ >= static_cast<double>(current->weight) &&
        std::none_of(waiters_.begin(), waiters_.end(), [current](const Waiter& waiter) {
            return waiter.priority < current->priority ||
                (waiter.priority == current->priority && waiter.sequence < current->sequence);
        });
}

std::chrono::steady_clock::duration GmgnRequestScheduler::WaitForTokens(const std::size_t weight) const noexcept {
    const auto missing = (std::max)(0.0, static_cast<double>(weight) - availableTokens_);
    const auto seconds = missing / static_cast<double>(tokensPerSecond_);
    return (std::max)(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double>(seconds)),
                      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::milliseconds{1}));
}

std::optional<GmgnRequestScheduler::Permit> GmgnRequestScheduler::Acquire(
    const Priority priority, const std::size_t weight, const std::stop_token stop) {
    if (!weight || weight > tokenCapacity_) {
        throw std::invalid_argument("GMGN request weight exceeds scheduler capacity.");
    }
    std::unique_lock lock(mutex_);
    if (cancelled_ || stop.stop_requested()) return std::nullopt;
    const auto sequence = nextSequence_++;
    waiters_.push_back({sequence, priority, weight});
    wake_.notify_all();
    std::stop_callback stopCallback(stop, [this] { wake_.notify_all(); });
    while (!cancelled_ && !stop.stop_requested()) {
        Refill(std::chrono::steady_clock::now());
        if (MayRun(sequence)) {
            const auto waiter = std::find_if(waiters_.begin(), waiters_.end(), [sequence](const Waiter& value) { return value.sequence == sequence; });
            availableTokens_ -= static_cast<double>(waiter->weight);
            waiters_.erase(waiter);
            ++active_;
            return Permit(this);
        }
        const auto current = std::find_if(waiters_.begin(), waiters_.end(), [sequence](const Waiter& value) { return value.sequence == sequence; });
        if (current == waiters_.end()) return std::nullopt;
        const bool nextByPriority = std::none_of(waiters_.begin(), waiters_.end(), [current](const Waiter& waiter) {
            return waiter.priority < current->priority ||
                (waiter.priority == current->priority && waiter.sequence < current->sequence);
        });
        if (active_ < maximumActive_ && nextByPriority) wake_.wait_for(lock, WaitForTokens(current->weight));
        else wake_.wait(lock);
    }
    const auto waiter = std::find_if(waiters_.begin(), waiters_.end(), [sequence](const Waiter& value) { return value.sequence == sequence; });
    if (waiter != waiters_.end()) waiters_.erase(waiter);
    wake_.notify_all();
    return std::nullopt;
}
void GmgnRequestScheduler::Release() noexcept { std::scoped_lock lock(mutex_); if (active_) --active_; wake_.notify_all(); }
std::size_t GmgnRequestScheduler::ActiveCount() const { std::scoped_lock lock(mutex_); return active_; }
std::size_t GmgnRequestScheduler::PendingCount() const { std::scoped_lock lock(mutex_); return waiters_.size(); }
void GmgnRequestScheduler::CancelPending() noexcept { std::scoped_lock lock(mutex_); cancelled_ = true; wake_.notify_all(); }
void GmgnRequestScheduler::Reset() noexcept {
    std::scoped_lock lock(mutex_);
    cancelled_ = false;
    waiters_.clear();
    availableTokens_ = static_cast<double>(tokenCapacity_);
    lastRefill_ = std::chrono::steady_clock::now();
    wake_.notify_all();
}

TokenAnalysisService::TokenAnalysisService(std::shared_ptr<IGmgnClient> client, GmgnRequestScheduler& scheduler, INotificationService& notifications, IAlertClock& clock, const std::chrono::seconds cooldown) : client_(std::move(client)), scheduler_(scheduler), notifications_(notifications), clock_(clock), cooldown_(cooldown) {}
EnrichmentFacts TokenAnalysisService::ToFacts(const TokenInfo& info, const TokenSecurity& security) { return {security.honeypot, security.openSource, security.renounced, security.buyTax, security.sellTax, info.lockedRatio, info.gmgnLink}; }
std::string TokenAnalysisService::FailureDiagnostic(const GmgnFailure& failure) { return failure.code == GmgnFailureCode::Authentication ? "GMGN authentication is required before token enrichment can continue." : failure.code == GmgnFailureCode::RateLimited ? "GMGN rate limiting delayed token enrichment." : "Token enrichment did not return the required GMGN data."; }
AnalysisUpdate TokenAnalysisService::FailureUpdate(const GmgnFailure& failure) {
    const bool retryable = failure.code == GmgnFailureCode::RateLimited || failure.code == GmgnFailureCode::Timeout || failure.code == GmgnFailureCode::TransientNetworkOrServer;
    return {false, false, retryable, FailureDiagnostic(failure), std::nullopt,
        retryable ? failure.retryAfter : std::nullopt, failure.code == GmgnFailureCode::Authentication};
}
AnalysisUpdate TokenAnalysisService::Analyze(const FrozenTokenCluster& cluster, const std::stop_token stop) {
    if (!client_ || stop.stop_requested()) return {false, false, false, "Token enrichment was cancelled."};
    GmgnResult<TokenInfo> infoResult;
    { const auto infoPermit = scheduler_.Acquire(GmgnRequestScheduler::Priority::Enrichment, GmgnRequestScheduler::kEnrichmentWeight, stop); if (!infoPermit) return {false, false, false, "Token enrichment was cancelled before GMGN admission."}; infoResult = client_->FetchTokenInfo(cluster.token, stop); }
    if (const auto* failure = std::get_if<GmgnFailure>(&infoResult)) return FailureUpdate(*failure);
    const auto& info = std::get<TokenInfo>(infoResult); if (info.token != cluster.token) return {false, false, false, "GMGN token-info did not match the frozen cluster token."};
    GmgnResult<TokenSecurity> securityResult;
    { const auto securityPermit = scheduler_.Acquire(GmgnRequestScheduler::Priority::Enrichment, GmgnRequestScheduler::kEnrichmentWeight, stop); if (!securityPermit) return {false, false, false, "Token enrichment was cancelled before GMGN admission."}; securityResult = client_->FetchTokenSecurity(cluster.token, stop); }
    if (const auto* failure = std::get_if<GmgnFailure>(&securityResult)) return FailureUpdate(*failure);
    const auto& security = std::get<TokenSecurity>(securityResult); if (security.token != cluster.token) return {false, false, false, "GMGN token-security did not match the frozen cluster token."};
    TokenAlert alert = ComposeAlert(cluster, info.sanitizedSymbol, ToFacts(info, security)); if (alert.validatedGmgnUrl.empty() && !info.gmgnLink.empty()) alert.body += "; GMGN link unavailable";
    const auto now = clock_.SteadyNow(); if (cooldown_.IsActive(cluster.token, now)) return {false, true, false, "Token alert suppressed by the active cooldown.", std::move(alert)};
    if (!notifications_.Show(alert)) return {false, false, false, "Windows notification service rejected the enriched alert.", std::move(alert)};
    cooldown_.MarkDelivered(cluster.token, now); return {true, false, false, {}, std::move(alert)};
}
void TokenAnalysisService::ClearSession() noexcept { cooldown_.Clear(); }
void TokenAnalysisService::SetCooldownDuration(const std::chrono::seconds duration) { cooldown_ = CooldownManager(duration); }
TokenAnalysisExecutor::TokenAnalysisExecutor(TokenAnalysisService& service, UpdateHandler handler) : service_(service), handler_(std::move(handler)) {}
TokenAnalysisExecutor::~TokenAnalysisExecutor() { Stop(); }
bool TokenAnalysisExecutor::Start() { std::scoped_lock lock(mutex_); if (worker_.joinable()) return false; accepting_ = true; worker_ = std::jthread([this](const std::stop_token stop) { Run(stop); }); return true; }
bool TokenAnalysisExecutor::TrySubmit(FrozenTokenCluster cluster) { std::scoped_lock lock(mutex_); if (!accepting_ || pending_.size() == kCapacity) return false; pending_.push_back({std::move(cluster), 0, std::chrono::steady_clock::now()}); wake_.notify_one(); return true; }
bool TokenAnalysisExecutor::Submit(FrozenTokenCluster cluster, const std::stop_token stop) { std::unique_lock lock(mutex_); wake_.wait(lock, stop, [this] { return !accepting_ || pending_.size() < kCapacity; }); if (!accepting_ || stop.stop_requested()) return false; pending_.push_back({std::move(cluster), 0, std::chrono::steady_clock::now()}); wake_.notify_one(); return true; }
void TokenAnalysisExecutor::RequestStop() noexcept { { std::scoped_lock lock(mutex_); accepting_ = false; wake_.notify_all(); } if (worker_.joinable()) { worker_.request_stop(); wake_.notify_all(); } }
void TokenAnalysisExecutor::Stop() noexcept { RequestStop(); if (worker_.joinable()) worker_.join(); std::scoped_lock lock(mutex_); pending_.clear(); service_.ClearSession(); }
std::chrono::seconds TokenAnalysisExecutor::RetryDelay(const std::size_t retryAttempt) noexcept { constexpr std::array<std::chrono::seconds, 6> delays{std::chrono::seconds{1}, std::chrono::seconds{2}, std::chrono::seconds{4}, std::chrono::seconds{8}, std::chrono::seconds{16}, std::chrono::seconds{30}}; return delays[(std::min)(retryAttempt, delays.size() - 1)]; }
void TokenAnalysisExecutor::Run(const std::stop_token stop) {
    while (!stop.stop_requested()) {
        PendingAnalysis next;
        { std::unique_lock lock(mutex_); while (!stop.stop_requested()) { if (pending_.empty()) { wake_.wait(lock, stop, [this] { return !pending_.empty() || !accepting_; }); if (!accepting_ && pending_.empty()) return; continue; } const auto earliest = std::min_element(pending_.begin(), pending_.end(), [](const PendingAnalysis& left, const PendingAnalysis& right) { return left.notBefore < right.notBefore; }); const auto now = std::chrono::steady_clock::now(); if (earliest->notBefore > now) { wake_.wait_until(lock, stop, earliest->notBefore, [] { return false; }); continue; } next = std::move(*earliest); pending_.erase(earliest); wake_.notify_all(); break; } if (stop.stop_requested()) return; }
        auto update = service_.Analyze(next.cluster, stop);
        if (update.retryable && next.retryAttempts >= kMaximumRetryAttempts) {
            update.retryable = false;
            update.retryAfter.reset();
            update.diagnostic += " Token enrichment retry limit was reached.";
        }
        if (handler_) handler_(update);
        if (update.retryable && !stop.stop_requested()) { const auto delay = update.retryAfter.value_or(RetryDelay(next.retryAttempts)); std::scoped_lock lock(mutex_); if (accepting_) { pending_.push_back({std::move(next.cluster), next.retryAttempts + 1, std::chrono::steady_clock::now() + delay}); wake_.notify_one(); } }
    }
}
} // namespace gmemmonitor::core
