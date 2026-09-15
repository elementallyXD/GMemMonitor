#include "gmemmonitor/core/Analysis.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gmemmonitor::core {
GmgnRequestScheduler::Permit::Permit(Permit&& other) noexcept : owner_(std::exchange(other.owner_, nullptr)) {}
GmgnRequestScheduler::Permit& GmgnRequestScheduler::Permit::operator=(Permit&& other) noexcept { if (this != &other) { if (owner_) owner_->Release(); owner_ = std::exchange(other.owner_, nullptr); } return *this; }
GmgnRequestScheduler::Permit::~Permit() { if (owner_) owner_->Release(); }
GmgnRequestScheduler::GmgnRequestScheduler(const std::size_t maximumActive) : maximumActive_(maximumActive) { if (!maximumActive) throw std::invalid_argument("GMGN scheduler needs an active slot."); }
bool GmgnRequestScheduler::MayRun(const std::size_t sequence) const noexcept { if (active_ >= maximumActive_) return false; const auto current = std::find_if(waiters_.begin(), waiters_.end(), [sequence](const Waiter& waiter) { return waiter.sequence == sequence; }); return current != waiters_.end() && std::none_of(waiters_.begin(), waiters_.end(), [current](const Waiter& waiter) { return waiter.priority < current->priority || (waiter.priority == current->priority && waiter.sequence < current->sequence); }); }
std::optional<GmgnRequestScheduler::Permit> GmgnRequestScheduler::Acquire(const Priority priority, const std::stop_token stop) { std::unique_lock lock(mutex_); if (cancelled_ || stop.stop_requested()) return std::nullopt; const auto sequence = nextSequence_++; waiters_.push_back({sequence, priority}); wake_.wait(lock, stop, [this, sequence] { return cancelled_ || MayRun(sequence); }); const auto waiter = std::find_if(waiters_.begin(), waiters_.end(), [sequence](const Waiter& value) { return value.sequence == sequence; }); if (waiter == waiters_.end() || cancelled_ || stop.stop_requested()) { if (waiter != waiters_.end()) waiters_.erase(waiter); wake_.notify_all(); return std::nullopt; } waiters_.erase(waiter); ++active_; return Permit(this); }
void GmgnRequestScheduler::Release() noexcept { std::scoped_lock lock(mutex_); if (active_) --active_; wake_.notify_all(); }
std::size_t GmgnRequestScheduler::ActiveCount() const { std::scoped_lock lock(mutex_); return active_; }
std::size_t GmgnRequestScheduler::PendingCount() const { std::scoped_lock lock(mutex_); return waiters_.size(); }
void GmgnRequestScheduler::CancelPending() noexcept { std::scoped_lock lock(mutex_); cancelled_ = true; wake_.notify_all(); }

TokenAnalysisService::TokenAnalysisService(std::shared_ptr<IGmgnClient> client, GmgnRequestScheduler& scheduler, INotificationService& notifications, IAlertClock& clock, const std::chrono::seconds cooldown) : client_(std::move(client)), scheduler_(scheduler), notifications_(notifications), clock_(clock), cooldown_(cooldown) {}
EnrichmentFacts TokenAnalysisService::ToFacts(const TokenInfo& info, const TokenSecurity& security) { return {security.honeypot, security.openSource, security.renounced, security.buyTax, security.sellTax, info.lockedRatio, info.gmgnLink}; }
std::string TokenAnalysisService::FailureDiagnostic(const GmgnFailure& failure) { return failure.code == GmgnFailureCode::Authentication ? "GMGN authentication is required before token enrichment can continue." : failure.code == GmgnFailureCode::RateLimited ? "GMGN rate limiting delayed token enrichment." : "Token enrichment did not return the required GMGN data."; }
AnalysisUpdate TokenAnalysisService::Analyze(const FrozenTokenCluster& cluster, const std::stop_token stop) {
    if (!client_ || stop.stop_requested()) return {false, false, "Token enrichment was cancelled."};
    GmgnResult<TokenInfo> infoResult;
    { const auto infoPermit = scheduler_.Acquire(GmgnRequestScheduler::Priority::Enrichment, stop); if (!infoPermit) return {false, false, "Token enrichment was cancelled before GMGN admission."}; infoResult = client_->FetchTokenInfo(cluster.token, stop); }
    if (const auto* failure = std::get_if<GmgnFailure>(&infoResult)) return {false, false, FailureDiagnostic(*failure)};
    const auto& info = std::get<TokenInfo>(infoResult); if (info.token != cluster.token) return {false, false, "GMGN token-info did not match the frozen cluster token."};
    GmgnResult<TokenSecurity> securityResult;
    { const auto securityPermit = scheduler_.Acquire(GmgnRequestScheduler::Priority::Enrichment, stop); if (!securityPermit) return {false, false, "Token enrichment was cancelled before GMGN admission."}; securityResult = client_->FetchTokenSecurity(cluster.token, stop); }
    if (const auto* failure = std::get_if<GmgnFailure>(&securityResult)) return {false, false, FailureDiagnostic(*failure)};
    const auto& security = std::get<TokenSecurity>(securityResult); if (security.token != cluster.token) return {false, false, "GMGN token-security did not match the frozen cluster token."};
    TokenAlert alert = ComposeAlert(cluster, info.sanitizedSymbol, ToFacts(info, security)); if (alert.validatedGmgnUrl.empty() && !info.gmgnLink.empty()) alert.body += "; GMGN link unavailable";
    const auto now = clock_.SteadyNow(); if (cooldown_.IsActive(cluster.token, now)) return {false, true, "Token alert suppressed by the active cooldown.", std::move(alert)};
    if (!notifications_.Show(alert)) return {false, false, "Windows notification service rejected the enriched alert.", std::move(alert)};
    cooldown_.MarkDelivered(cluster.token, now); return {true, false, {}, std::move(alert)};
}
void TokenAnalysisService::ClearSession() noexcept { cooldown_.Clear(); }
TokenAnalysisExecutor::TokenAnalysisExecutor(TokenAnalysisService& service, UpdateHandler handler) : service_(service), handler_(std::move(handler)) {}
TokenAnalysisExecutor::~TokenAnalysisExecutor() { Stop(); }
bool TokenAnalysisExecutor::Start() { if (worker_.joinable()) return false; worker_ = std::jthread([this](const std::stop_token stop) { Run(stop); }); return true; }
bool TokenAnalysisExecutor::TrySubmit(FrozenTokenCluster cluster) { std::scoped_lock lock(mutex_); if (!worker_.joinable() || pending_.size() == kCapacity) return false; pending_.push_back(std::move(cluster)); wake_.notify_one(); return true; }
void TokenAnalysisExecutor::Stop() noexcept { if (worker_.joinable()) { worker_.request_stop(); wake_.notify_all(); worker_.join(); } std::scoped_lock lock(mutex_); pending_.clear(); service_.ClearSession(); }
void TokenAnalysisExecutor::Run(const std::stop_token stop) { while (!stop.stop_requested()) { std::optional<FrozenTokenCluster> next; { std::unique_lock lock(mutex_); wake_.wait(lock, stop, [this] { return !pending_.empty(); }); if (stop.stop_requested()) return; next = std::move(pending_.front()); pending_.pop_front(); } const auto update = service_.Analyze(*next, stop); if (handler_) handler_(update); } }
} // namespace gmemmonitor::core
