#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace gmemmonitor::core {

enum class ProcessTerminationReason { Completed, TimedOut, Cancelled, StartFailed, OutputLimitExceeded, WaitFailed };

struct CapturedOutput final {
    std::string text;
    std::size_t totalBytes{};
    bool truncated{};
};

struct ProcessRequest final {
    std::filesystem::path executable;
    std::filesystem::path workingDirectory;
    std::vector<std::wstring> arguments;
    std::chrono::milliseconds timeout{30'000};
    std::size_t maximumStdoutBytes{1024 * 1024};
    std::size_t maximumStderrBytes{1024 * 1024};
};

struct ProcessResult final {
    ProcessTerminationReason reason{ProcessTerminationReason::StartFailed};
    unsigned long exitCode{};
    unsigned long win32Error{};
    std::chrono::milliseconds duration{};
    CapturedOutput stdoutOutput;
    CapturedOutput stderrOutput;
};

struct IProcessRunner {
    virtual ~IProcessRunner() = default;
    virtual ProcessResult Run(const ProcessRequest& request, std::stop_token stop) = 0;
};

class ProcessRunner final : public IProcessRunner {
public:
    ProcessResult Run(const ProcessRequest& request, std::stop_token stop) override;
};

} // namespace gmemmonitor::core
