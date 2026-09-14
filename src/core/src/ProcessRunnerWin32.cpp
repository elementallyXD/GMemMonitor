#include "gmemmonitor/core/Process.h"

#include <windows.h>

#include <atomic>
#include <thread>
#include <string_view>

namespace gmemmonitor::core {
namespace {

class Handle final {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.Release()) {}
    HANDLE Get() const noexcept { return value_; }
    HANDLE Release() noexcept { const HANDLE value = value_; value_ = nullptr; return value; }
private: HANDLE value_{};
};

[[nodiscard]] std::wstring Quote(const std::wstring_view value) {
    if (value.find_first_of(L" \t\"") == std::wstring_view::npos) return std::wstring(value);
    std::wstring quoted{L"\""}; std::size_t backslashes{};
    for (const wchar_t character : value) {
        if (character == L'\\') { ++backslashes; continue; }
        if (character == L'\"') quoted.append(backslashes * 2 + 1, L'\\');
        else quoted.append(backslashes, L'\\');
        quoted.push_back(character); backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\'); quoted.push_back(L'\"'); return quoted;
}

[[nodiscard]] std::wstring CommandLine(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments) {
    std::wstring command = Quote(executable.wstring());
    for (const auto& argument : arguments) { command.push_back(L' '); command += Quote(argument); }
    return command;
}

void Drain(HANDLE pipe, CapturedOutput* output, const std::size_t limit, std::atomic_bool* exceeded) {
    char buffer[4096]; DWORD read{};
    while (ReadFile(pipe, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr) && read != 0) {
        output->totalBytes += read;
        const std::size_t available = output->text.size() < limit ? limit - output->text.size() : 0;
        const std::size_t copied = (std::min)(available, static_cast<std::size_t>(read));
        output->text.append(buffer, copied);
        if (copied != read) { output->truncated = true; exceeded->store(true); }
    }
}

} // namespace

ProcessResult ProcessRunner::Run(const ProcessRequest& request, const std::stop_token stop) {
    const auto started = std::chrono::steady_clock::now(); ProcessResult result;
    if (!request.executable.is_absolute() || !request.workingDirectory.is_absolute()) { result.win32Error = ERROR_BAD_PATHNAME; return result; }

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE}; HANDLE stdoutRead{}, stdoutWrite{}, stderrRead{}, stderrWrite{};
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &security, 0) || !CreatePipe(&stderrRead, &stderrWrite, &security, 0) ||
        !SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0) || !SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0)) { result.win32Error = GetLastError(); return result; }
    Handle outRead(stdoutRead), outWrite(stdoutWrite), errRead(stderrRead), errWrite(stderrWrite);
    STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESTDHANDLES; startup.hStdOutput = outWrite.Get(); startup.hStdError = errWrite.Get(); startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION info{}; std::wstring command = CommandLine(request.executable, request.arguments);
    if (!CreateProcessW(request.executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, request.workingDirectory.c_str(), &startup, &info)) { result.win32Error = GetLastError(); return result; }
    Handle process(info.hProcess), thread(info.hThread);
    CloseHandle(outWrite.Release());
    CloseHandle(errWrite.Release());
    Handle job(CreateJobObjectW(nullptr, nullptr)); JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.Get() || !SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) || !AssignProcessToJobObject(job.Get(), process.Get())) { result.win32Error = GetLastError(); TerminateProcess(process.Get(), ERROR_CANCELLED); return result; }
    std::atomic_bool exceeded{}; std::thread output(Drain, outRead.Get(), &result.stdoutOutput, request.maximumStdoutBytes, &exceeded); std::thread error(Drain, errRead.Get(), &result.stderrOutput, request.maximumStderrBytes, &exceeded);
    const auto deadline = started + request.timeout;
    for (;;) {
        const DWORD wait = WaitForSingleObject(process.Get(), 25);
        if (wait == WAIT_OBJECT_0) { result.reason = exceeded ? ProcessTerminationReason::OutputLimitExceeded : ProcessTerminationReason::Completed; break; }
        if (wait == WAIT_FAILED) { result.reason = ProcessTerminationReason::WaitFailed; result.win32Error = GetLastError(); TerminateJobObject(job.Get(), ERROR_CANCELLED); break; }
        if (stop.stop_requested()) { result.reason = ProcessTerminationReason::Cancelled; TerminateJobObject(job.Get(), ERROR_CANCELLED); break; }
        if (exceeded || std::chrono::steady_clock::now() >= deadline) { result.reason = exceeded ? ProcessTerminationReason::OutputLimitExceeded : ProcessTerminationReason::TimedOut; TerminateJobObject(job.Get(), ERROR_TIMEOUT); break; }
    }
    WaitForSingleObject(process.Get(), INFINITE); GetExitCodeProcess(process.Get(), &result.exitCode); output.join(); error.join();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started); return result;
}

} // namespace gmemmonitor::core
