#include <windows.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace {
void WriteRepeated(HANDLE handle, const char byte, const std::size_t count, const bool partial) {
    for (std::size_t written = 0; written < count;) {
        const DWORD chunk = static_cast<DWORD>((std::min)(partial ? std::size_t{7} : std::size_t{4096}, count - written));
        DWORD result{};
        if (!WriteFile(handle, std::string(chunk, byte).data(), chunk, &result, nullptr)) return;
        written += result;
        if (partial) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
}

[[nodiscard]] std::wstring Argument(const int argc, wchar_t* argv[], const int index) {
    return index < argc ? argv[index] : L"";
}
}

int wmain(const int argc, wchar_t* argv[]) {
    const auto mode = Argument(argc, argv, 1);
    if (mode == L"valid-json") { WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "{\"ok\":true}\n", 12, nullptr, nullptr); return 0; }
    if (mode == L"large-stdout") { WriteRepeated(GetStdHandle(STD_OUTPUT_HANDLE), 'o', 32 * 1024, false); return 0; }
    if (mode == L"large-stderr") { WriteRepeated(GetStdHandle(STD_ERROR_HANDLE), 'e', 32 * 1024, false); return 0; }
    if (mode == L"partial-writes") { WriteRepeated(GetStdHandle(STD_OUTPUT_HANDLE), 'p', 128, true); return 0; }
    if (mode == L"warning") { WriteFile(GetStdHandle(STD_ERROR_HANDLE), "warning\n", 8, nullptr, nullptr); return 0; }
    if (mode == L"nonzero") { WriteFile(GetStdHandle(STD_ERROR_HANDLE), "failed\n", 7, nullptr, nullptr); return 7; }
    if (mode == L"sanitized-environment") {
        wchar_t value[8]{};
        const bool leaked = GetEnvironmentVariableW(L"GMEMMONITOR_TEST_SECRET", value, static_cast<DWORD>(std::size(value))) != 0;
        DWORD bytesRead{};
        char input{};
        const bool nullInput = ReadFile(GetStdHandle(STD_INPUT_HANDLE), &input, 1, &bytesRead, nullptr) != FALSE && bytesRead == 0;
        const char* message = !leaked && nullInput ? "clean\n" : "unsafe\n";
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), message, static_cast<DWORD>(strlen(message)), nullptr, nullptr);
        return !leaked && nullInput ? 0 : 10;
    }
    if (mode == L"hang") { Sleep(INFINITE); }
    if (mode == L"child-sleep") {
        std::ofstream(Argument(argc, argv, 2)) << GetCurrentProcessId();
        Sleep(INFINITE);
    }
    if (mode == L"spawn-child") {
        const auto marker = Argument(argc, argv, 2);
        std::wstring command = L"\"" + std::filesystem::path(argv[0]).wstring() + L"\" child-sleep \"" + marker + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return 8;
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        Sleep(INFINITE);
    }
    return 9;
}
