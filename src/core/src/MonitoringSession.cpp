#include "gmemmonitor/core/MonitoringSession.h"

namespace gmemmonitor::core {

MonitoringSession::MonitoringSession(std::shared_ptr<IGmgnClient> client, INotificationService& notifications,
                                     IClock& utcClock, IAlertClock& alertClock,
                                     MonitoringHandler monitoringHandler, AnalysisHandler analysisHandler)
    : client_(std::move(client)),
      monitoringHandler_(std::move(monitoringHandler)),
      analysisHandler_(std::move(analysisHandler)),
      controller_(utcClock),
      analysisService_(client_, scheduler_, notifications, alertClock),
      analysisExecutor_(analysisService_, [this](const AnalysisUpdate& update) { HandleAnalysisUpdate(update); }),
      poller_(client_, controller_, [this](const MonitoringUpdate& update) {
                  if (monitoringHandler_) monitoringHandler_(update);
              }, &scheduler_,
              [this](FrozenTokenCluster cluster, const std::stop_token stop) {
                  return analysisExecutor_.Submit(std::move(cluster), stop);
              }) {}

MonitoringSession::~MonitoringSession() { Stop(); }

bool MonitoringSession::Start(const AppSettings& settings) {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return false;
    scheduler_.Reset();
    analysisService_.SetCooldownDuration(settings.notificationCooldown);
    if (!analysisExecutor_.Start()) {
        started_.store(false);
        return false;
    }
    if (poller_.Start(settings)) return true;
    analysisExecutor_.Stop();
    started_.store(false);
    return false;
}

void MonitoringSession::RequestStop() noexcept {
    scheduler_.CancelPending();
    poller_.RequestStop();
    analysisExecutor_.RequestStop();
}

void MonitoringSession::Stop() noexcept {
    RequestStop();
    poller_.Stop();
    analysisExecutor_.Stop();
    started_.store(false);
}

MonitoringState MonitoringSession::State() const noexcept { return controller_.State(); }

void MonitoringSession::HandleAnalysisUpdate(const AnalysisUpdate& update) {
    if (update.authenticationRequired) {
        // This callback runs on the analysis worker. Signal the entire session,
        // including this executor, without trying to join the current thread.
        RequestStop();
        if (monitoringHandler_) {
            monitoringHandler_({MonitoringState::AuthenticationRequired, {},
                "GMGN authentication is required before monitoring can continue."});
        }
    }
    if (analysisHandler_) analysisHandler_(update);
}

} // namespace gmemmonitor::core
