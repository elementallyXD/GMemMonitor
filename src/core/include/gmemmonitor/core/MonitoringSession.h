#pragma once

#include "gmemmonitor/core/Analysis.h"
#include "gmemmonitor/core/Monitoring.h"

#include <functional>
#include <memory>

namespace gmemmonitor::core {

// Owns one complete, non-UI monitoring session. Its handlers run on worker
// threads; a platform layer must marshal them before updating presentation state.
class MonitoringSession final {
public:
    using MonitoringHandler = std::function<void(const MonitoringUpdate&)>;
    using AnalysisHandler = std::function<void(const AnalysisUpdate&)>;

    MonitoringSession(std::shared_ptr<IGmgnClient> client, INotificationService& notifications,
                      IClock& utcClock, IAlertClock& alertClock,
                      MonitoringHandler monitoringHandler = {}, AnalysisHandler analysisHandler = {});
    ~MonitoringSession();
    MonitoringSession(const MonitoringSession&) = delete;
    MonitoringSession& operator=(const MonitoringSession&) = delete;

    [[nodiscard]] bool Start(const AppSettings& settings);
    void Stop() noexcept;
    [[nodiscard]] MonitoringState State() const noexcept;

private:
    std::shared_ptr<IGmgnClient> client_;
    GmgnRequestScheduler scheduler_;
    MonitoringController controller_;
    TokenAnalysisService analysisService_;
    TokenAnalysisExecutor analysisExecutor_;
    WalletActivityPoller poller_;
};

} // namespace gmemmonitor::core
