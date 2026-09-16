#include "gmemmonitor/core/Process.h"

#include <windows.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <memory>
#include <thread>
#include <string_view>
#include <vector>

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

class AttributeList final {
public:
    AttributeList() {
        SIZE_T bytes{};
        static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes));
        storage_.resize(bytes);
        list_ = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(list_, 1, 0, &bytes)) list_ = nullptr;
    }
    ~AttributeList() { if (list_) DeleteProcThreadAttributeList(list_); }
    AttributeList(const AttributeList&) = delete;
    AttributeList& operator=(const AttributeList&) = delete;
    [[nodiscard]] PPROC_THREAD_ATTRIBUTE_LIST Get() const noexcept { return list_; }
private:
    std::vector<std::byte> storage_;
    PPROC_THREAD_ATTRIBUTE_LIST list_{};
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

[[nodiscard]] std::optional<std::wstring> EnvironmentValue(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return std::nullopt;
    std::wstring value(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0 || written >= needed) return std::nullopt;
    value.resize(written);
    return value;
}

// The CLI intentionally receives only the Windows/user locations needed by Node
// and by its user-owned ~/.config/gmgn/.env lookup. Arbitrary parent-process
// secrets and development variables are not inherited.
[[nodiscard]] std::vector<wchar_t> SanitizedEnvironment() {
    constexpr std::array<const wchar_t*, 9> names{
        L"APPDATA", L"HOMEDRIVE", L"HOMEPATH", L"LOCALAPPDATA", L"SystemRoot",
        L"TEMP", L"TMP", L"USERPROFILE", L"windir"
    };
    std::vector<std::wstring> entries;
    for (const auto name : names) {
        if (const auto value = EnvironmentValue(name)) entries.emplace_back(std::wstring(name) + L"=" + *value);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });
    std::vector<wchar_t> block;
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

} // namespace

ProcessResult ProcessRunner::Run(const ProcessRequest& request, const std::stop_token stop) {
    const auto started = std::chrono::steady_clock::now(); ProcessResult result;
    if (!request.executable.is_absolute() || !request.workingDirectory.is_absolute() ||
        !std::filesystem::is_regular_file(request.executable) || !std::filesystem::is_directory(request.workingDirectory)) {
        result.win32Error = ERROR_BAD_PATHNAME;
        return result;
    }

    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE stdoutReadRaw{}, stdoutWriteRaw{};
    if (!CreatePipe(&stdoutReadRaw, &stdoutWriteRaw, &security, 0)) { result.win32Error = GetLastError(); return result; }
    Handle outRead(stdoutReadRaw), outWrite(stdoutWriteRaw);
    HANDLE stderrReadRaw{}, stderrWriteRaw{};
    if (!CreatePipe(&stderrReadRaw, &stderrWriteRaw, &security, 0)) { result.win32Error = GetLastError(); return result; }
    Handle errRead(stderrReadRaw), errWrite(stderrWriteRaw);
    if (!SetHandleInformation(outRead.Get(), HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(errRead.Get(), HANDLE_FLAG_INHERIT, 0)) {
        result.win32Error = GetLastError();
        return result;
    }
    Handle nullInput(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!nullInput.Get() || nullInput.Get() == INVALID_HANDLE_VALUE) { result.win32Error = GetLastError(); return result; }

    AttributeList attributes;
    if (!attributes.Get()) { result.win32Error = GetLastError(); return result; }
    std::array<HANDLE, 3> inheritedHandles{nullInput.Get(), outWrite.Get(), errWrite.Get()};
    if (!UpdateProcThreadAttribute(attributes.Get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inheritedHandles.data(), inheritedHandles.size() * sizeof(HANDLE), nullptr, nullptr)) {
        result.win32Error = GetLastError();
        return result;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = outWrite.Get();
    startup.StartupInfo.hStdError = errWrite.Get();
    startup.StartupInfo.hStdInput = nullInput.Get();
    startup.lpAttributeList = attributes.Get();
    PROCESS_INFORMATION info{};
    std::wstring command = CommandLine(request.executable, request.arguments);
    auto environment = SanitizedEnvironment();
    constexpr DWORD creationFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED |
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
    if (!CreateProcessW(request.executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        creationFlags, environment.data(), request.workingDirectory.c_str(),
        &startup.StartupInfo, &info)) {
        result.win32Error = GetLastError();
        return result;
    }
    Handle process(info.hProcess), thread(info.hThread);
    CloseHandle(outWrite.Release());
    CloseHandle(errWrite.Release());
    CloseHandle(nullInput.Release());
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job.Get() || !SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job.Get(), process.Get())) {
        result.win32Error = GetLastError();
        TerminateProcess(process.Get(), ERROR_CANCELLED);
        return result;
    }
    std::atomic_bool exceeded{};
    std::thread output(Drain, outRead.Get(), &result.stdoutOutput, request.maximumStdoutBytes, &exceeded);
    std::thread error(Drain, errRead.Get(), &result.stderrOutput, request.maximumStderrBytes, &exceeded);
    if (ResumeThread(thread.Get()) == static_cast<DWORD>(-1)) {
        result.win32Error = GetLastError();
        TerminateJobObject(job.Get(), ERROR_CANCELLED);
        WaitForSingleObject(process.Get(), INFINITE);
        output.join();
        error.join();
        return result;
    }
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
