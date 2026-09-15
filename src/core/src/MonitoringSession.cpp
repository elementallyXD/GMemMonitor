#include "gmemmonitor/core/MonitoringSession.h"

namespace gmemmonitor::core {

MonitoringSession::MonitoringSession(std::shared_ptr<IGmgnClient> client, INotificationService& notifications,
                                     IClock& utcClock, IAlertClock& alertClock,
                                     MonitoringHandler monitoringHandler, AnalysisHandler analysisHandler)
    : client_(std::move(client)),
      controller_(utcClock),
      analysisService_(client_, scheduler_, notifications, alertClock),
      analysisExecutor_(analysisService_, std::move(analysisHandler)),
      poller_(client_, controller_, std::move(monitoringHandler), &scheduler_,
              [this](FrozenTokenCluster cluster, const std::stop_token stop) {
                  return analysisExecutor_.Submit(std::move(cluster), stop);
              }) {}

MonitoringSession::~MonitoringSession() { Stop(); }

bool MonitoringSession::Start(const AppSettings& settings) {
    analysisService_.SetCooldownDuration(settings.notificationCooldown);
    if (!analysisExecutor_.Start()) return false;
    if (poller_.Start(settings)) return true;
    analysisExecutor_.Stop();
    return false;
}

void MonitoringSession::Stop() noexcept {
    poller_.Stop();
    analysisExecutor_.Stop();
}

MonitoringState MonitoringSession::State() const noexcept { return controller_.State(); }

} // namespace gmemmonitor::core
