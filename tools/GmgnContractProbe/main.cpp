#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kMaximumCapturedOutputBytes = 1024 * 1024;
constexpr DWORD kDefaultTimeoutMilliseconds = 30'000;

enum class Action {
    Version,
    ConfigCheck,
    FollowWallet,
    TokenInfo,
    TokenSecurity,
};

struct Options final {
    bool showHelp{false};
    bool selfTest{false};
    std::filesystem::path nodePath;
    std::filesystem::path cliEntryPath;
    Action action{Action::Version};
    std::optional<std::wstring> token;
    std::optional<std::filesystem::path> sanitizedFixturePath;
    DWORD timeoutMilliseconds{kDefaultTimeoutMilliseconds};
};

struct CapturedStream final {
    std::string data;
    std::uint64_t totalBytes{0};
    bool truncated{false};
};

struct ProcessResult final {
    DWORD exitCode{0};
    DWORD win32Error{ERROR_SUCCESS};
    bool started{false};
    bool timedOut{false};
    bool assignedToJob{false};
    std::chrono::milliseconds duration{};
    CapturedStream stdoutStream;
    CapturedStream stderrStream;
};

[[nodiscard]] bool ContainsAsciiInsensitive(const std::string_view text, const std::string_view needle) {
    if (needle.empty() || needle.size() > text.size()) {
        return needle.empty();
    }

    return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
        [](const char left, const char right) {
            return std::tolower(static_cast<unsigned char>(left)) ==
                   std::tolower(static_cast<unsigned char>(right));
        }) != text.end();
}

[[nodiscard]] bool IsRateLimited(const ProcessResult& result) {
    return ContainsAsciiInsensitive(result.stderrStream.data, "http 429") ||
           ContainsAsciiInsensitive(result.stderrStream.data, "rate_limit_exceeded") ||
           ContainsAsciiInsensitive(result.stdoutStream.data, "rate_limit_exceeded");
}

enum class SafeFailureCategory {
    None,
    RateLimited,
    CredentialConfiguration,
    Authentication,
    Transport,
    OtherChildFailure,
};

[[nodiscard]] std::string_view SafeFailureCategoryName(const SafeFailureCategory category) {
    switch (category) {
    case SafeFailureCategory::None: return "none";
    case SafeFailureCategory::RateLimited: return "rate limited";
    case SafeFailureCategory::CredentialConfiguration: return "external GMGN credential configuration unavailable";
    case SafeFailureCategory::Authentication: return "GMGN authentication rejected";
    case SafeFailureCategory::Transport: return "network or transport failure";
    case SafeFailureCategory::OtherChildFailure: return "unclassified GMGN CLI failure";
    }
    return "unclassified GMGN CLI failure";
}

[[nodiscard]] SafeFailureCategory ClassifyFailure(const ProcessResult& result) {
    if (IsRateLimited(result)) {
        return SafeFailureCategory::RateLimited;
    }

    const std::string combined = result.stdoutStream.data + "\n" + result.stderrStream.data;
    if (ContainsAsciiInsensitive(combined, "gmgn_api_key is required") ||
        ContainsAsciiInsensitive(combined, "gmgn_private_key is required") ||
        ContainsAsciiInsensitive(combined, "not configured")) {
        return SafeFailureCategory::CredentialConfiguration;
    }
    if (ContainsAsciiInsensitive(combined, "http 401") || ContainsAsciiInsensitive(combined, "http 403") ||
        ContainsAsciiInsensitive(combined, "unauthorized") || ContainsAsciiInsensitive(combined, "forbidden") ||
        ContainsAsciiInsensitive(combined, "invalid key") || ContainsAsciiInsensitive(combined, "invalid signature")) {
        return SafeFailureCategory::Authentication;
    }
    if (ContainsAsciiInsensitive(combined, "enotfound") || ContainsAsciiInsensitive(combined, "econn") ||
        ContainsAsciiInsensitive(combined, "etimedout") || ContainsAsciiInsensitive(combined, "network")) {
        return SafeFailureCategory::Transport;
    }
    return SafeFailureCategory::OtherChildFailure;
}

[[nodiscard]] std::optional<unsigned long> RateLimitRemainingSeconds(const ProcessResult& result) {
    constexpr std::string_view suffix{"s remaining"};
    const std::string_view text{result.stderrStream.data};
    const std::size_t suffixPosition = text.find(suffix);
    if (suffixPosition == std::string_view::npos) {
        return std::nullopt;
    }

    std::size_t end = suffixPosition;
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    std::size_t begin = end;
    while (begin > 0 && std::isdigit(static_cast<unsigned char>(text[begin - 1]))) {
        --begin;
    }
    if (begin == end) {
        return std::nullopt;
    }

    unsigned long seconds{};
    const auto [position, error] = std::from_chars(text.data() + begin, text.data() + end, seconds);
    return error == std::errc{} && position == text.data() + end ? std::optional<unsigned long>{seconds} : std::nullopt;
}

class UniqueHandle final {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle() { Reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.Release()) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE Get() const noexcept { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE Release() noexcept {
        HANDLE released = handle_;
        handle_ = nullptr;
        return released;
    }

    void Reset(HANDLE replacement = nullptr) noexcept {
        if (*this) {
            CloseHandle(handle_);
        }
        handle_ = replacement;
    }

private:
    HANDLE handle_{nullptr};
};

class JsonSyntaxValidator final {
public:
    explicit JsonSyntaxValidator(std::string_view input) : input_(input) {}

    [[nodiscard]] bool IsValid() {
        SkipWhitespace();
        if (!ParseValue()) {
            return false;
        }
        SkipWhitespace();
        return position_ == input_.size();
    }

private:
    [[nodiscard]] bool ParseValue() {
        if (position_ >= input_.size()) {
            return false;
        }

        switch (input_[position_]) {
        case '{': return ParseObject();
        case '[': return ParseArray();
        case '"': return ParseString();
        case 't': return ConsumeLiteral("true");
        case 'f': return ConsumeLiteral("false");
        case 'n': return ConsumeLiteral("null");
        default: return ParseNumber();
        }
    }

    [[nodiscard]] bool ParseObject() {
        ++position_;
        SkipWhitespace();
        if (Consume('}')) {
            return true;
        }

        while (true) {
            if (!ParseString()) {
                return false;
            }
            SkipWhitespace();
            if (!Consume(':')) {
                return false;
            }
            SkipWhitespace();
            if (!ParseValue()) {
                return false;
            }
            SkipWhitespace();
            if (Consume('}')) {
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
            SkipWhitespace();
        }
    }

    [[nodiscard]] bool ParseArray() {
        ++position_;
        SkipWhitespace();
        if (Consume(']')) {
            return true;
        }

        while (true) {
            if (!ParseValue()) {
                return false;
            }
            SkipWhitespace();
            if (Consume(']')) {
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
            SkipWhitespace();
        }
    }

    [[nodiscard]] bool ParseString() {
        if (!Consume('"')) {
            return false;
        }

        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                return true;
            }
            if (character < 0x20) {
                return false;
            }
            if (character != '\\') {
                continue;
            }
            if (position_ >= input_.size()) {
                return false;
            }
            const char escape = input_[position_++];
            if (escape == 'u') {
                for (int index = 0; index < 4; ++index) {
                    if (position_ >= input_.size() || !IsHex(input_[position_++])) {
                        return false;
                    }
                }
            } else if (std::string_view{"\"\\/bfnrt"}.find(escape) == std::string_view::npos) {
                return false;
            }
        }
        return false;
    }

    [[nodiscard]] bool ParseNumber() {
        const std::size_t start = position_;
        static_cast<void>(Consume('-'));

        if (Consume('0')) {
            // A leading zero is valid only as the complete integer part.
        } else {
            if (!ConsumeDigit('1', '9')) {
                position_ = start;
                return false;
            }
            while (position_ < input_.size() && IsDigit(input_[position_])) {
                ++position_;
            }
        }

        if (Consume('.')) {
            if (position_ >= input_.size() || !IsDigit(input_[position_])) {
                position_ = start;
                return false;
            }
            while (position_ < input_.size() && IsDigit(input_[position_])) {
                ++position_;
            }
        }

        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= input_.size() || !IsDigit(input_[position_])) {
                position_ = start;
                return false;
            }
            while (position_ < input_.size() && IsDigit(input_[position_])) {
                ++position_;
            }
        }

        return position_ > start;
    }

    [[nodiscard]] bool ConsumeLiteral(std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            return false;
        }
        position_ += literal.size();
        return true;
    }

    [[nodiscard]] bool Consume(char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] bool ConsumeDigit(char first, char last) {
        if (position_ >= input_.size() || input_[position_] < first || input_[position_] > last) {
            return false;
        }
        ++position_;
        return true;
    }

    void SkipWhitespace() {
        while (position_ < input_.size()) {
            const char character = input_[position_];
            if (character != ' ' && character != '\n' && character != '\r' && character != '\t') {
                break;
            }
            ++position_;
        }
    }

    [[nodiscard]] static bool IsDigit(char value) {
        return value >= '0' && value <= '9';
    }

    [[nodiscard]] static bool IsHex(char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
               (value >= 'A' && value <= 'F');
    }

    std::string_view input_;
    std::size_t position_{0};
};

// This deliberately keeps JSON object keys (the contract surface) but replaces every
// value.  The source response is never written to disk, printed, or logged.  Replacing
// every value rather than trying to recognize every identifier prevents a newly-added
// GMGN field from accidentally leaking account or transaction data into a fixture.
class JsonFixtureSanitizer final {
public:
    explicit JsonFixtureSanitizer(const std::string_view input) : input_(input) {}

    [[nodiscard]] bool Sanitize(std::string* output) {
        output->clear();
        SkipWhitespace();
        if (!ParseValue({}, output)) {
            return false;
        }
        SkipWhitespace();
        return position_ == input_.size();
    }

private:
    [[nodiscard]] bool ParseValue(const std::string_view propertyName, std::string* output) {
        if (position_ >= input_.size()) {
            return false;
        }

        switch (input_[position_]) {
        case '{': return ParseObject(output);
        case '[': return ParseArray(propertyName, output);
        case '"':
            return SkipString() && AppendString(ReplacementForString(propertyName), output);
        case 't':
            if (!ConsumeLiteral("true")) return false;
            output->append("false");
            return true;
        case 'f':
            if (!ConsumeLiteral("false")) return false;
            output->append("false");
            return true;
        case 'n':
            if (!ConsumeLiteral("null")) return false;
            output->append("null");
            return true;
        default:
            if (!SkipNumber()) return false;
            output->append(ReplacementForNumber(propertyName));
            return true;
        }
    }

    [[nodiscard]] bool ParseObject(std::string* output) {
        ++position_;
        output->push_back('{');
        SkipWhitespace();
        if (Consume('}')) {
            output->push_back('}');
            return true;
        }

        while (true) {
            std::string_view rawName;
            std::string propertyName;
            if (!ReadString(&rawName, &propertyName)) {
                return false;
            }
            output->append(rawName);
            SkipWhitespace();
            if (!Consume(':')) {
                return false;
            }
            output->push_back(':');
            SkipWhitespace();
            if (!ParseValue(propertyName, output)) {
                return false;
            }
            SkipWhitespace();
            if (Consume('}')) {
                output->push_back('}');
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
            output->push_back(',');
            SkipWhitespace();
        }
    }

    [[nodiscard]] bool ParseArray(const std::string_view propertyName, std::string* output) {
        ++position_;
        output->push_back('[');
        SkipWhitespace();
        if (Consume(']')) {
            output->push_back(']');
            return true;
        }

        while (true) {
            if (!ParseValue(propertyName, output)) {
                return false;
            }
            SkipWhitespace();
            if (Consume(']')) {
                output->push_back(']');
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
            output->push_back(',');
            SkipWhitespace();
        }
    }

    [[nodiscard]] bool ReadString(std::string_view* raw, std::string* decoded) {
        const std::size_t start = position_;
        if (!Consume('"')) {
            return false;
        }
        decoded->clear();
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                *raw = input_.substr(start, position_ - start);
                return true;
            }
            if (character < 0x20) {
                return false;
            }
            if (character != '\\') {
                decoded->push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size()) {
                return false;
            }
            const char escape = input_[position_++];
            switch (escape) {
            case '"': decoded->push_back('"'); break;
            case '\\': decoded->push_back('\\'); break;
            case '/': decoded->push_back('/'); break;
            case 'b': decoded->push_back('\b'); break;
            case 'f': decoded->push_back('\f'); break;
            case 'n': decoded->push_back('\n'); break;
            case 'r': decoded->push_back('\r'); break;
            case 't': decoded->push_back('\t'); break;
            case 'u':
                for (int index = 0; index < 4; ++index) {
                    if (position_ >= input_.size() || !IsHex(input_[position_++])) {
                        return false;
                    }
                }
                decoded->append("?");
                break;
            default:
                return false;
            }
        }
        return false;
    }

    [[nodiscard]] bool SkipString() {
        std::string_view ignoredRaw;
        std::string ignoredDecoded;
        return ReadString(&ignoredRaw, &ignoredDecoded);
    }

    [[nodiscard]] bool SkipNumber() {
        const std::size_t start = position_;
        static_cast<void>(Consume('-'));
        if (Consume('0')) {
            // A leading zero is valid only as the complete integer part.
        } else {
            if (!ConsumeDigit('1', '9')) {
                position_ = start;
                return false;
            }
            while (position_ < input_.size() && IsDigit(input_[position_])) {
                ++position_;
            }
        }
        if (Consume('.')) {
            if (position_ >= input_.size() || !IsDigit(input_[position_])) {
                position_ = start;
                return false;
            }
            while (position_ < input_.size() && IsDigit(input_[position_])) {
                ++position_;
            }
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= input_.size() || !IsDigit(input_[position_])) {
                position_ = start;
                return false;
            }
            while (position_ < input_.size() && IsDigit(input_[position_])) {
                ++position_;
            }
        }
        return position_ > start;
    }

    [[nodiscard]] bool ConsumeLiteral(const std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            return false;
        }
        position_ += literal.size();
        return true;
    }

    [[nodiscard]] bool Consume(const char expected) {
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] bool ConsumeDigit(const char first, const char last) {
        if (position_ >= input_.size() || input_[position_] < first || input_[position_] > last) {
            return false;
        }
        ++position_;
        return true;
    }

    static void SkipWhitespace(std::string_view input, std::size_t* position) {
        while (*position < input.size()) {
            const char character = input[*position];
            if (character != ' ' && character != '\n' && character != '\r' && character != '\t') {
                return;
            }
            ++*position;
        }
    }

    void SkipWhitespace() { SkipWhitespace(input_, &position_); }

    [[nodiscard]] static bool IsDigit(const char value) {
        return value >= '0' && value <= '9';
    }

    [[nodiscard]] static bool IsHex(const char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
               (value >= 'A' && value <= 'F');
    }

    [[nodiscard]] static std::string_view ReplacementForString(const std::string_view propertyName) {
        const auto contains = [propertyName](const std::string_view needle) {
            return ContainsAsciiInsensitive(propertyName, needle);
        };
        if (propertyName == "chain") {
            return "bsc";
        }
        if (propertyName == "side") {
            return "buy";
        }
        if (contains("address") || propertyName == "maker" || propertyName == "token" ||
            propertyName == "contract") {
            return "0x1111111111111111111111111111111111111111";
        }
        if (contains("hash")) {
            return "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        }
        if (contains("url") || contains("link")) {
            return "https://gmgn.ai/token/redacted";
        }
        if (contains("time")) {
            return "1700000000";
        }
        if (contains("usd")) {
            return "100.00";
        }
        if (contains("amount") || contains("price")) {
            return "1";
        }
        return "redacted";
    }

    [[nodiscard]] static std::string_view ReplacementForNumber(const std::string_view propertyName) {
        if (ContainsAsciiInsensitive(propertyName, "time")) {
            return "1700000000";
        }
        if (ContainsAsciiInsensitive(propertyName, "decimal")) {
            return "18";
        }
        if (ContainsAsciiInsensitive(propertyName, "usd")) {
            return "100";
        }
        return "1";
    }

    [[nodiscard]] static bool AppendString(const std::string_view value, std::string* output) {
        output->push_back('"');
        output->append(value);
        output->push_back('"');
        return true;
    }

    std::string_view input_;
    std::size_t position_{0};
};

[[nodiscard]] std::wstring QuoteWindowsArgument(const std::wstring& value) {
    if (value.empty()) {
        return L"\"\"";
    }

    const bool needsQuotes = value.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) {
        return value;
    }

    std::wstring quoted;
    quoted.push_back(L'\"');
    std::size_t backslashCount = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashCount;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashCount * 2 + 1, L'\\');
            quoted.push_back(L'\"');
            backslashCount = 0;
            continue;
        }
        quoted.append(backslashCount, L'\\');
        backslashCount = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashCount * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

[[nodiscard]] std::wstring BuildCommandLine(const std::vector<std::wstring>& arguments) {
    std::wstring commandLine;
    for (const std::wstring& argument : arguments) {
        if (!commandLine.empty()) {
            commandLine.push_back(L' ');
        }
        commandLine += QuoteWindowsArgument(argument);
    }
    return commandLine;
}

[[nodiscard]] bool IsExistingAbsoluteFile(const std::filesystem::path& path) {
    std::error_code error;
    return path.is_absolute() && std::filesystem::is_regular_file(path, error) && !error;
}

[[nodiscard]] bool IsApprovedFixtureOutputPath(const std::filesystem::path& path) {
    if (!path.is_absolute() || path.extension() != L".json") {
        return false;
    }

    std::error_code error;
    const auto expectedDirectory = std::filesystem::weakly_canonical(
        std::filesystem::current_path(error) / L"tests" / L"contract" / L"fixtures" / L"gmgn", error);
    if (error || !std::filesystem::is_directory(expectedDirectory, error) || error) {
        return false;
    }
    const auto actualDirectory = std::filesystem::weakly_canonical(path.parent_path(), error);
    return !error && actualDirectory == expectedDirectory && !std::filesystem::exists(path, error) && !error;
}

[[nodiscard]] bool WriteNewSanitizedFixture(const std::filesystem::path& path, const std::string_view document) {
    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        return false;
    }

    std::size_t written = 0;
    while (written < document.size()) {
        const auto remaining = document.size() - written;
        const DWORD chunkSize = static_cast<DWORD>(std::min<std::size_t>(remaining, MAXDWORD));
        DWORD bytesWritten = 0;
        if (WriteFile(file.Get(), document.data() + written, chunkSize, &bytesWritten, nullptr) == FALSE || bytesWritten == 0) {
            file.Reset();
            DeleteFileW(path.c_str());
            return false;
        }
        written += bytesWritten;
    }
    if (FlushFileBuffers(file.Get()) == FALSE) {
        file.Reset();
        DeleteFileW(path.c_str());
        return false;
    }
    return true;
}

[[nodiscard]] bool IsValidBscAddress(std::wstring_view value) {
    if (value.size() != 42 || value[0] != L'0' || (value[1] != L'x' && value[1] != L'X')) {
        return false;
    }
    return std::all_of(value.begin() + 2, value.end(), [](wchar_t character) {
        return (character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f') ||
               (character >= L'A' && character <= L'F');
    });
}

void DrainPipe(HANDLE pipe, CapturedStream* stream) {
    std::array<char, 8192> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) != FALSE) {
        stream->totalBytes += bytesRead;
        const std::size_t available = kMaximumCapturedOutputBytes - stream->data.size();
        const std::size_t copyCount = std::min<std::size_t>(available, bytesRead);
        stream->data.append(buffer.data(), copyCount);
        if (copyCount < bytesRead) {
            stream->truncated = true;
        }
    }
}

[[nodiscard]] ProcessResult RunChildProcess(const std::vector<std::wstring>& arguments, DWORD timeoutMilliseconds) {
    ProcessResult result;
    SECURITY_ATTRIBUTES inheritableSecurity{};
    inheritableSecurity.nLength = sizeof(inheritableSecurity);
    inheritableSecurity.bInheritHandle = TRUE;

    HANDLE stdoutReadRaw = nullptr;
    HANDLE stdoutWriteRaw = nullptr;
    HANDLE stderrReadRaw = nullptr;
    HANDLE stderrWriteRaw = nullptr;
    if (CreatePipe(&stdoutReadRaw, &stdoutWriteRaw, &inheritableSecurity, 0) == FALSE ||
        CreatePipe(&stderrReadRaw, &stderrWriteRaw, &inheritableSecurity, 0) == FALSE) {
        result.win32Error = GetLastError();
        if (stdoutReadRaw != nullptr) CloseHandle(stdoutReadRaw);
        if (stdoutWriteRaw != nullptr) CloseHandle(stdoutWriteRaw);
        if (stderrReadRaw != nullptr) CloseHandle(stderrReadRaw);
        if (stderrWriteRaw != nullptr) CloseHandle(stderrWriteRaw);
        return result;
    }

    UniqueHandle stdoutRead(stdoutReadRaw);
    UniqueHandle stdoutWrite(stdoutWriteRaw);
    UniqueHandle stderrRead(stderrReadRaw);
    UniqueHandle stderrWrite(stderrWriteRaw);
    if (SetHandleInformation(stdoutRead.Get(), HANDLE_FLAG_INHERIT, 0) == FALSE ||
        SetHandleInformation(stderrRead.Get(), HANDLE_FLAG_INHERIT, 0) == FALSE) {
        result.win32Error = GetLastError();
        return result;
    }

    UniqueHandle nullInput(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        &inheritableSecurity, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!nullInput) {
        result.win32Error = GetLastError();
        return result;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = nullInput.Get();
    startupInfo.hStdOutput = stdoutWrite.Get();
    startupInfo.hStdError = stderrWrite.Get();

    PROCESS_INFORMATION processInformation{};
    std::wstring commandLine = BuildCommandLine(arguments);
    const auto startedAt = std::chrono::steady_clock::now();
    if (CreateProcessW(arguments.front().c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInformation) == FALSE) {
        result.win32Error = GetLastError();
        return result;
    }
    result.started = true;
    UniqueHandle process(processInformation.hProcess);
    UniqueHandle primaryThread(processInformation.hThread);

    stdoutWrite.Reset();
    stderrWrite.Reset();
    nullInput.Reset();

    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (!job) {
        result.win32Error = GetLastError();
        TerminateProcess(process.Get(), ERROR_CANCELLED);
        return result;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == FALSE ||
        AssignProcessToJobObject(job.Get(), process.Get()) == FALSE) {
        result.win32Error = GetLastError();
        TerminateProcess(process.Get(), ERROR_CANCELLED);
        return result;
    }
    result.assignedToJob = true;

    std::thread stdoutReader(DrainPipe, stdoutRead.Get(), &result.stdoutStream);
    std::thread stderrReader(DrainPipe, stderrRead.Get(), &result.stderrStream);

    const DWORD waitResult = WaitForSingleObject(process.Get(), timeoutMilliseconds);
    if (waitResult == WAIT_TIMEOUT) {
        result.timedOut = true;
        TerminateJobObject(job.Get(), ERROR_TIMEOUT);
        WaitForSingleObject(process.Get(), INFINITE);
    } else if (waitResult == WAIT_FAILED) {
        result.win32Error = GetLastError();
        TerminateJobObject(job.Get(), ERROR_CANCELLED);
        WaitForSingleObject(process.Get(), INFINITE);
    }

    GetExitCodeProcess(process.Get(), &result.exitCode);
    stdoutReader.join();
    stderrReader.join();
    stdoutRead.Reset();
    stderrRead.Reset();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt);
    return result;
}

[[nodiscard]] std::optional<Action> ParseAction(std::wstring_view value) {
    if (value == L"version") return Action::Version;
    if (value == L"config-check") return Action::ConfigCheck;
    if (value == L"follow-wallet") return Action::FollowWallet;
    if (value == L"token-info") return Action::TokenInfo;
    if (value == L"token-security") return Action::TokenSecurity;
    return std::nullopt;
}

[[nodiscard]] bool ParseUnsigned(std::wstring_view value, DWORD* output) {
    if (value.empty()) {
        return false;
    }
    std::uint64_t parsed = 0;
    for (const wchar_t character : value) {
        if (character < L'0' || character > L'9') {
            return false;
        }
        const auto digit = static_cast<std::uint64_t>(character - L'0');
        if (parsed > ((300U - digit) / 10U)) {
            return false;
        }
        parsed = (parsed * 10U) + digit;
    }
    if (parsed == 0) {
        return false;
    }
    *output = static_cast<DWORD>(parsed * 1000);
    return true;
}

[[nodiscard]] bool ActionExpectsRawJson(Action action);

[[nodiscard]] bool ParseOptions(int argc, wchar_t* argv[], Options* options, std::wstring* error) {
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument = argv[index];
        const auto requireValue = [&](std::wstring_view option) -> std::optional<std::wstring_view> {
            if (++index >= argc) {
                *error = L"Missing value for " + std::wstring(option) + L".";
                return std::nullopt;
            }
            return argv[index];
        };

        if (argument == L"--help" || argument == L"-h") {
            options->showHelp = true;
        } else if (argument == L"--self-test") {
            options->selfTest = true;
        } else if (argument == L"--node") {
            const auto value = requireValue(argument);
            if (!value) return false;
            options->nodePath = *value;
        } else if (argument == L"--cli-entry") {
            const auto value = requireValue(argument);
            if (!value) return false;
            options->cliEntryPath = *value;
        } else if (argument == L"--action") {
            const auto value = requireValue(argument);
            if (!value) return false;
            const auto parsed = ParseAction(*value);
            if (!parsed) {
                *error = L"Unknown action: " + std::wstring(*value) + L".";
                return false;
            }
            options->action = *parsed;
        } else if (argument == L"--token") {
            const auto value = requireValue(argument);
            if (!value) return false;
            options->token = std::wstring(*value);
        } else if (argument == L"--sanitized-fixture") {
            const auto value = requireValue(argument);
            if (!value) return false;
            options->sanitizedFixturePath = std::filesystem::path(*value);
        } else if (argument == L"--timeout-seconds") {
            const auto value = requireValue(argument);
            if (!value || !ParseUnsigned(*value, &options->timeoutMilliseconds)) {
                *error = L"--timeout-seconds must be an integer from 1 to 300.";
                return false;
            }
        } else {
            *error = L"Unknown option: " + std::wstring(argument) + L".";
            return false;
        }
    }

    if (options->showHelp || options->selfTest) {
        return true;
    }
    if (!IsExistingAbsoluteFile(options->nodePath) || !IsExistingAbsoluteFile(options->cliEntryPath)) {
        *error = L"--node and --cli-entry must be absolute paths to existing files.";
        return false;
    }
    if ((options->action == Action::TokenInfo || options->action == Action::TokenSecurity) &&
        (!options->token || !IsValidBscAddress(*options->token))) {
        *error = L"--token must be a BSC address in 0x + 40 hexadecimal character form.";
        return false;
    }
    if ((options->action == Action::Version || options->action == Action::ConfigCheck || options->action == Action::FollowWallet) && options->token) {
        *error = L"--token is valid only with token-info or token-security.";
        return false;
    }
    if (options->sanitizedFixturePath && !ActionExpectsRawJson(options->action)) {
        *error = L"--sanitized-fixture is valid only with follow-wallet, token-info, or token-security.";
        return false;
    }
    if (options->sanitizedFixturePath && !IsApprovedFixtureOutputPath(*options->sanitizedFixturePath)) {
        *error = L"--sanitized-fixture must be a new absolute .json file directly under tests\\contract\\fixtures\\gmgn from the repository root.";
        return false;
    }
    return true;
}

void PrintUsage() {
    std::wcout << LR"(GmgnContractProbe

Usage:
  GmgnContractProbe --self-test
  GmgnContractProbe --node <absolute-node.exe> --cli-entry <absolute-index.js>
                    --action <version|config-check|follow-wallet|token-info|token-security>
                    [--token <0x...>] [--sanitized-fixture <absolute-fixture.json>]
                    [--timeout-seconds <1..300>]

The probe never reads, writes, prints, or accepts GMGN credentials. Configure the CLI externally.
)";
}

[[nodiscard]] bool RunSelfTest() {
    const std::array<std::pair<std::string_view, bool>, 8> jsonCases{{
        {R"({"list":[1,true,null,"ok"]})", true},
        {R"([{"amount_usd":"100.00"}])", true},
        {R"({"escaped":"\\u0041"})", true},
        {R"({"unterminated":)", false},
        {R"([1,])", false},
        {R"({"leading":01})", false},
        {R"({"control":"
"})", false},
        {R"({"exponent":1e-3})", true},
    }};

    bool passed = true;
    for (const auto& [document, expected] : jsonCases) {
        if (JsonSyntaxValidator(document).IsValid() != expected) {
            passed = false;
        }
    }
    passed = passed && IsValidBscAddress(L"0x0000000000000000000000000000000000000000") &&
             !IsValidBscAddress(L"0x000000000000000000000000000000000000000") &&
             !IsValidBscAddress(L"0x000000000000000000000000000000000000000g");
    DWORD timeout{};
    passed = passed && ParseUnsigned(L"1", &timeout) && timeout == 1'000 &&
             ParseUnsigned(L"300", &timeout) && timeout == 300'000 &&
             !ParseUnsigned(L"0", &timeout) && !ParseUnsigned(L"301", &timeout) &&
             !ParseUnsigned(L"999999999999999999999999", &timeout);
    ProcessResult rateLimited;
    rateLimited.stderrStream.data =
        "HTTP 429 code=429 error=RATE_LIMIT_EXCEEDED message=IP rate limit exceeded (~28s remaining).";
    passed = passed && IsRateLimited(rateLimited) && RateLimitRemainingSeconds(rateLimited) == 28UL &&
             ClassifyFailure(rateLimited) == SafeFailureCategory::RateLimited;
    ProcessResult missingCredentials;
    missingCredentials.exitCode = 1;
    missingCredentials.stderrStream.data = "GMGN_PRIVATE_KEY is required for critical-auth commands";
    passed = passed && ClassifyFailure(missingCredentials) == SafeFailureCategory::CredentialConfiguration;

    constexpr std::string_view privateFixtureCandidate =
        R"({"list":[{"chain":"bsc","side":"buy","maker":"0x0123456789abcdef0123456789abcdef01234567","transaction_hash":"0xfeedbeef","amount_usd":"1234.56","honeypot":true,"name":"Private Wallet Label"}],"next_page_token":"account-specific-token"})";
    std::string sanitizedFixture;
    JsonFixtureSanitizer fixtureSanitizer(privateFixtureCandidate);
    passed = passed && fixtureSanitizer.Sanitize(&sanitizedFixture) &&
             JsonSyntaxValidator(sanitizedFixture).IsValid() &&
             sanitizedFixture.find("0123456789abcdef") == std::string::npos &&
             sanitizedFixture.find("Private Wallet Label") == std::string::npos &&
             sanitizedFixture.find("account-specific-token") == std::string::npos &&
             sanitizedFixture.find("\"chain\":\"bsc\"") != std::string::npos &&
             sanitizedFixture.find("\"side\":\"buy\"") != std::string::npos &&
             sanitizedFixture.find("\"maker\":\"0x1111111111111111111111111111111111111111\"") != std::string::npos &&
             sanitizedFixture.find("\"amount_usd\":\"100.00\"") != std::string::npos &&
             sanitizedFixture.find("\"honeypot\":false") != std::string::npos;
    std::wcout << (passed ? L"Self-test passed.\n" : L"Self-test failed.\n");
    return passed;
}

[[nodiscard]] std::vector<std::wstring> BuildCliArguments(const Options& options) {
    std::vector<std::wstring> arguments{options.nodePath.wstring(), options.cliEntryPath.wstring()};
    switch (options.action) {
    case Action::Version:
        arguments.emplace_back(L"--version");
        break;
    case Action::ConfigCheck:
        arguments.emplace_back(L"config");
        arguments.emplace_back(L"--check");
        break;
    case Action::FollowWallet:
        arguments.emplace_back(L"track");
        arguments.emplace_back(L"follow-wallet");
        arguments.emplace_back(L"--chain");
        arguments.emplace_back(L"bsc");
        arguments.emplace_back(L"--side");
        arguments.emplace_back(L"buy");
        arguments.emplace_back(L"--limit");
        arguments.emplace_back(L"100");
        arguments.emplace_back(L"--raw");
        break;
    case Action::TokenInfo:
    case Action::TokenSecurity:
        arguments.emplace_back(L"token");
        arguments.emplace_back(options.action == Action::TokenInfo ? L"info" : L"security");
        arguments.emplace_back(L"--chain");
        arguments.emplace_back(L"bsc");
        arguments.emplace_back(L"--address");
        arguments.push_back(*options.token);
        arguments.emplace_back(L"--raw");
        break;
    }
    return arguments;
}

[[nodiscard]] bool ActionExpectsRawJson(Action action) {
    return action == Action::FollowWallet || action == Action::TokenInfo || action == Action::TokenSecurity;
}

void PrintSafeResult(const ProcessResult& result, bool expectsJson, const Action action) {
    std::wcout << L"Process started: " << (result.started ? L"yes" : L"no") << L"\n";
    std::wcout << L"Assigned to Job Object: " << (result.assignedToJob ? L"yes" : L"no") << L"\n";
    std::wcout << L"Timed out: " << (result.timedOut ? L"yes" : L"no") << L"\n";
    std::wcout << L"Exit code: " << result.exitCode << L"\n";
    std::wcout << L"Duration: " << result.duration.count() << L" ms\n";
    std::wcout << L"Stdout bytes: " << result.stdoutStream.totalBytes
               << (result.stdoutStream.truncated ? L" (capture truncated)" : L"") << L"\n";
    std::wcout << L"Stderr bytes: " << result.stderrStream.totalBytes
               << (result.stderrStream.truncated ? L" (capture truncated)" : L"") << L"\n";
    if (result.win32Error != ERROR_SUCCESS) {
        std::wcout << L"Win32 error: " << result.win32Error << L"\n";
    }
    if (expectsJson && result.exitCode == 0 && !result.timedOut) {
        std::cout << "Raw stdout is syntactically valid JSON: "
                  << (JsonSyntaxValidator(result.stdoutStream.data).IsValid() ? "yes" : "no") << '\n';
    }
    if (action == Action::ConfigCheck && !result.timedOut && result.win32Error == ERROR_SUCCESS) {
        std::wcout << L"External GMGN API-key configuration detected: " << (result.exitCode == 0 ? L"yes" : L"no") << L"\n";
    }
    if (IsRateLimited(result)) {
        std::wcout << L"GMGN rate limit detected: yes\n";
        if (const auto remaining = RateLimitRemainingSeconds(result)) {
            std::wcout << L"Reported remaining cooldown: approximately " << *remaining << L" seconds\n";
        }
        std::wcout << L"Do not rerun the probe until the GMGN cooldown has elapsed.\n";
    }
    if (!result.timedOut && (result.win32Error != ERROR_SUCCESS || result.exitCode != 0)) {
        const auto category = SafeFailureCategoryName(ClassifyFailure(result));
        std::wcout << L"Safe failure category: " << std::wstring(category.begin(), category.end()) << L"\n";
    }
    std::wcout << L"Raw process output intentionally not displayed.\n";
}

[[nodiscard]] bool CreateSanitizedFixture(const ProcessResult& result, const std::filesystem::path& path) {
    if (!result.started || result.timedOut || result.win32Error != ERROR_SUCCESS || result.exitCode != 0) {
        std::wcout << L"Sanitized fixture was not created because the GMGN command did not succeed.\n";
        return false;
    }
    if (result.stdoutStream.truncated || !JsonSyntaxValidator(result.stdoutStream.data).IsValid()) {
        std::wcout << L"Sanitized fixture was not created because the complete stdout response was not valid JSON.\n";
        return false;
    }

    std::string sanitizedDocument;
    JsonFixtureSanitizer sanitizer(result.stdoutStream.data);
    if (!sanitizer.Sanitize(&sanitizedDocument) || !JsonSyntaxValidator(sanitizedDocument).IsValid() ||
        !WriteNewSanitizedFixture(path, sanitizedDocument)) {
        std::wcout << L"Sanitized fixture was not created. The raw response was discarded.\n";
        return false;
    }

    std::wcout << L"Sanitized fixture created: " << path.wstring() << L"\n";
    std::wcout << L"All response values were replaced; inspect only the retained JSON keys and value types.\n";
    return true;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    Options options;
    std::wstring error;
    if (!ParseOptions(argc, argv, &options, &error)) {
        std::wcerr << L"Error: " << error << L"\n\n";
        PrintUsage();
        return 2;
    }
    if (options.showHelp) {
        PrintUsage();
        return 0;
    }
    if (options.selfTest) {
        return RunSelfTest() ? 0 : 1;
    }

    const ProcessResult result = RunChildProcess(BuildCliArguments(options), options.timeoutMilliseconds);
    PrintSafeResult(result, ActionExpectsRawJson(options.action), options.action);
    const bool childSucceeded = result.started && !result.timedOut && result.win32Error == ERROR_SUCCESS && result.exitCode == 0;
    const bool fixtureSucceeded = !options.sanitizedFixturePath || CreateSanitizedFixture(result, *options.sanitizedFixturePath);
    return childSucceeded && fixtureSucceeded ? 0 : 1;
}
