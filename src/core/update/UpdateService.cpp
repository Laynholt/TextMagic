#include "UpdateService.h"

#include "AppVersion.h"
#include "EncodingUtils.h"
#include "Localization.h"
#include "PowerShellUtils.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
const wchar_t* T(const wchar_t* key) {
    return Localization::GetTextByName(key);
}

constexpr wchar_t kGitHubHost[] = L"github.com";
constexpr wchar_t kLatestReleasePath[] = L"/" TM_GITHUB_OWNER_W L"/" TM_GITHUB_REPO_W L"/releases/latest";
constexpr wchar_t kReleaseDownloadPrefix[] = L"/" TM_GITHUB_OWNER_W L"/" TM_GITHUB_REPO_W L"/releases/download/";
constexpr wchar_t kReleaseExeName[] = TM_APP_NAME_W L".exe";
constexpr wchar_t kSha256SumsName[] = L"SHA256SUMS.txt";
constexpr wchar_t kUserAgent[] = TM_APP_NAME_W L"-Updater/" TM_APP_VERSION_W;
constexpr int kWinHttpTimeoutMs = 15000;

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr)
        : m_handle(handle) {
    }

    ~WinHttpHandle() {
        if (m_handle) {
            WinHttpCloseHandle(m_handle);
            m_handle = nullptr;
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept
        : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }

    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        if (m_handle) {
            WinHttpCloseHandle(m_handle);
        }
        m_handle = other.m_handle;
        other.m_handle = nullptr;
        return *this;
    }

    HINTERNET get() const {
        return m_handle;
    }

    explicit operator bool() const {
        return m_handle != nullptr;
    }

private:
    HINTERNET m_handle;
};

bool SetWinHttpTimeouts(HINTERNET handle) {
    return handle != nullptr
        && WinHttpSetTimeouts(
            handle,
            kWinHttpTimeoutMs,
            kWinHttpTimeoutMs,
            kWinHttpTimeoutMs,
            kWinHttpTimeoutMs
        ) != FALSE;
}

std::wstring FormatWin32Error(DWORD errorCode) {
    wchar_t* buffer = nullptr;
    const DWORD chars = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );

    if (chars == 0 || !buffer) {
        std::wstringstream stream;
        stream << T(L"update.error.code_prefix") << errorCode;
        return stream.str();
    }

    std::wstring message(buffer, chars);
    LocalFree(buffer);

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

bool ComputeSha256(const std::wstring& filePath,
                   std::string& hashHex,
                   std::wstring& errorMessage) {
    hashHex.clear();
    HANDLE file = CreateFileW(
        filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (file == INVALID_HANDLE_VALUE) {
        errorMessage = std::wstring(T(L"update.error.checksum_file_open_prefix"))
            + L" " + FormatWin32Error(GetLastError());
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD bytesReturned = 0;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0
    );
    if (status >= 0) {
        status = BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
            &bytesReturned, 0
        );
    }

    std::vector<UCHAR> hashObject(objectLength);
    if (status >= 0) {
        status = BCryptCreateHash(
            algorithm, &hash, hashObject.data(), objectLength,
            nullptr, 0, 0
        );
    }

    std::array<BYTE, 64 * 1024> buffer = {};
    while (status >= 0) {
        DWORD bytesRead = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr)) {
            errorMessage = std::wstring(T(L"update.error.checksum_file_read_prefix"))
                + L" " + FormatWin32Error(GetLastError());
            status = -1;
            break;
        }
        if (bytesRead == 0) {
            break;
        }
        status = BCryptHashData(hash, buffer.data(), bytesRead, 0);
    }

    std::array<UCHAR, 32> digest = {};
    if (status >= 0) {
        status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    }

    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    CloseHandle(file);

    if (status < 0) {
        if (errorMessage.empty()) {
            errorMessage = T(L"update.error.checksum_compute");
        }
        return false;
    }

    constexpr char digits[] = "0123456789abcdef";
    hashHex.reserve(digest.size() * 2);
    for (UCHAR byte : digest) {
        hashHex.push_back(digits[byte >> 4]);
        hashHex.push_back(digits[byte & 0x0f]);
    }
    return true;
}

bool VerifyExpectedSha256(const std::wstring& filePath,
                          const std::wstring& expectedSha256,
                          std::wstring& errorMessage) {
    if (expectedSha256.size() != 64
        || !std::all_of(expectedSha256.begin(), expectedSha256.end(), [](wchar_t ch) {
            return iswxdigit(ch) != 0;
        })) {
        errorMessage = T(L"update.error.checksum_mismatch");
        return false;
    }

    std::string actualHash;
    if (!ComputeSha256(filePath, actualHash, errorMessage)) {
        return false;
    }
    std::string expectedHash;
    expectedHash.reserve(expectedSha256.size());
    for (wchar_t ch : expectedSha256) {
        expectedHash.push_back(static_cast<char>(ch));
    }
    std::transform(expectedHash.begin(), expectedHash.end(), expectedHash.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (actualHash != expectedHash) {
        errorMessage = T(L"update.error.checksum_mismatch");
        return false;
    }
    return true;
}

bool QueryRequestOptionString(HINTERNET request, DWORD option, std::wstring& value) {
    DWORD bytes = 0;
    if (!WinHttpQueryOption(request, option, WINHTTP_NO_OUTPUT_BUFFER, &bytes)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return false;
        }
    }

    if (bytes == 0) {
        return false;
    }

    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(request, option, buffer.data(), &bytes)) {
        return false;
    }

    value.assign(buffer.c_str());
    return !value.empty();
}

bool QueryLocationHeader(HINTERNET request, std::wstring& location) {
    DWORD bytes = 0;
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_LOCATION,
            WINHTTP_HEADER_NAME_BY_INDEX,
            WINHTTP_NO_OUTPUT_BUFFER,
            &bytes,
            WINHTTP_NO_HEADER_INDEX)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return false;
        }
    }

    if (bytes == 0) {
        return false;
    }

    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_LOCATION,
            WINHTTP_HEADER_NAME_BY_INDEX,
            buffer.data(),
            &bytes,
            WINHTTP_NO_HEADER_INDEX)) {
        return false;
    }

    location.assign(buffer.c_str());
    return !location.empty();
}

std::wstring ExtractTagFromUrl(const std::wstring& url) {
    const std::wstring token = L"/releases/tag/";
    const size_t tagStart = url.find(token);
    if (tagStart == std::wstring::npos) {
        return L"";
    }

    size_t valueStart = tagStart + token.size();
    size_t valueEnd = url.find_first_of(L"?#", valueStart);
    if (valueEnd == std::wstring::npos) {
        valueEnd = url.size();
    }
    if (valueEnd <= valueStart) {
        return L"";
    }

    std::wstring tag = url.substr(valueStart, valueEnd - valueStart);
    while (!tag.empty() && tag.back() == L'/') {
        tag.pop_back();
    }
    return tag;
}

std::vector<std::wstring> ParseVersionParts(const std::wstring& version) {
    std::vector<std::wstring> parts;
    std::wstring current;

    for (wchar_t ch : version) {
        if (ch >= L'0' && ch <= L'9') {
            current.push_back(ch);
            continue;
        }

        if (!current.empty()) {
            const size_t firstNonZero = current.find_first_not_of(L'0');
            parts.push_back(firstNonZero == std::wstring::npos
                                ? L"0"
                                : current.substr(firstNonZero));
            current.clear();
        }

        if (ch != L'.') {
            break;
        }
    }

    if (!current.empty()) {
        const size_t firstNonZero = current.find_first_not_of(L'0');
        parts.push_back(firstNonZero == std::wstring::npos
                            ? L"0"
                            : current.substr(firstNonZero));
    }

    return parts;
}

} // namespace

UpdateCheckResult UpdateService::CheckForUpdates(const std::wstring& currentVersion) const {
    UpdateCheckResult result = {};

    std::wstring latestTag;
    if (!ResolveLatestReleaseTag(latestTag, result.errorMessage)) {
        result.success = false;
        return result;
    }

    const std::wstring latestVersion = NormalizeVersionFromTag(latestTag);
    const int compareResult = CompareVersions(currentVersion, latestVersion);

    result.success = true;
    result.updateAvailable = (compareResult < 0);
    result.latestTag = latestTag;
    result.latestVersion = latestVersion;
    return result;
}

bool UpdateService::VerifySha256SumsFile(const std::wstring& filePath,
                                        const std::wstring& sumsPath,
                                        const std::wstring& entryName,
                                        std::wstring& errorMessage,
                                        std::wstring* verifiedSha256) {
    errorMessage.clear();
    if (filePath.empty() || sumsPath.empty() || entryName.empty()) {
        errorMessage = T(L"update.error.invalid_checksum_params");
        return false;
    }

    std::ifstream input(std::filesystem::path(sumsPath), std::ios::binary);
    if (!input) {
        errorMessage = T(L"update.error.checksum_read");
        return false;
    }

    const std::string expectedName = EncodingUtils::WideToUtf8(entryName);
    std::string expectedHash;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() < 66 || line[64] != ' ') {
            continue;
        }
        const std::string candidateHash = line.substr(0, 64);
        if (!std::all_of(candidateHash.begin(), candidateHash.end(), [](unsigned char ch) {
                return std::isxdigit(ch) != 0;
            })) {
            continue;
        }
        size_t nameStart = 64;
        while (nameStart < line.size() && line[nameStart] == ' ') {
            ++nameStart;
        }
        if (nameStart < line.size() && line[nameStart] == '*') {
            ++nameStart;
        }
        if (_stricmp(line.substr(nameStart).c_str(), expectedName.c_str()) == 0) {
            expectedHash = candidateHash;
            std::transform(expectedHash.begin(), expectedHash.end(), expectedHash.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            break;
        }
    }

    if (expectedHash.empty()) {
        errorMessage = std::wstring(T(L"update.error.checksum_entry_missing_prefix"))
            + L" " + entryName;
        return false;
    }

    std::string actualHash;
    if (!ComputeSha256(filePath, actualHash, errorMessage)) {
        return false;
    }
    if (actualHash != expectedHash) {
        errorMessage = T(L"update.error.checksum_mismatch");
        return false;
    }
    if (verifiedSha256) {
        verifiedSha256->assign(expectedHash.begin(), expectedHash.end());
    }
    return true;
}

bool UpdateService::DownloadReleaseExecutable(const std::wstring& tag,
                                              const std::wstring& destinationPath,
                                              std::wstring& verifiedSha256,
                                              std::wstring& errorMessage) const {
    errorMessage.clear();
    verifiedSha256.clear();
    if (tag.empty() || destinationPath.empty()) {
        errorMessage = T(L"update.error.invalid_download_params");
        return false;
    }
    const std::wstring sumsPath = destinationPath + L".sha256";
    DeleteFileW(destinationPath.c_str());
    DeleteFileW(sumsPath.c_str());

    if (!DownloadReleaseAsset(tag, kSha256SumsName, sumsPath, errorMessage)) {
        return false;
    }
    if (!DownloadReleaseAsset(tag, kReleaseExeName, destinationPath, errorMessage)) {
        DeleteFileW(sumsPath.c_str());
        return false;
    }

    const bool verified = VerifySha256SumsFile(
        destinationPath, sumsPath, kReleaseExeName, errorMessage, &verifiedSha256
    );
    DeleteFileW(sumsPath.c_str());
    if (!verified) {
        DeleteFileW(destinationPath.c_str());
    }
    return verified;
}

bool UpdateService::DownloadReleaseAsset(const std::wstring& tag,
                                         const std::wstring& assetName,
                                         const std::wstring& destinationPath,
                                         std::wstring& errorMessage) const {
    if (tag.empty() || assetName.empty() || destinationPath.empty()) {
        errorMessage = T(L"update.error.invalid_download_params");
        return false;
    }

    const std::wstring requestPath = std::wstring(kReleaseDownloadPrefix) + tag + L"/" + assetName;

    WinHttpHandle session(WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    ));
    if (!session) {
        errorMessage = std::wstring(T(L"update.error.winhttp_init_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }
    if (!SetWinHttpTimeouts(session.get())) {
        errorMessage = std::wstring(T(L"update.error.winhttp_init_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    WinHttpHandle connection(WinHttpConnect(session.get(), kGitHubHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        errorMessage = std::wstring(T(L"update.error.github_connect_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.get(),
        L"GET",
        requestPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    ));
    if (!request) {
        errorMessage = std::wstring(T(L"update.error.http_request_create_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }
    if (!SetWinHttpTimeouts(request.get())) {
        errorMessage = std::wstring(T(L"update.error.http_request_create_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    if (!WinHttpSendRequest(
            request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        errorMessage = std::wstring(T(L"update.error.send_download_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        errorMessage = std::wstring(T(L"update.error.receive_response_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
        errorMessage = std::wstring(T(L"update.error.http_status_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    if (statusCode != 200) {
        std::wstringstream stream;
        stream << T(L"update.error.server_returned_http_prefix")
               << statusCode
               << T(L"update.error.server_returned_http_suffix");
        errorMessage = stream.str();
        return false;
    }

    HANDLE fileHandle = CreateFileW(
        destinationPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (fileHandle == INVALID_HANDLE_VALUE) {
        errorMessage = std::wstring(T(L"update.error.update_file_create_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    bool readOk = true;
    while (true) {
        DWORD bytesAvailable = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &bytesAvailable)) {
            errorMessage = std::wstring(T(L"update.error.query_data_prefix")) + FormatWin32Error(GetLastError());
            readOk = false;
            break;
        }

        if (bytesAvailable == 0) {
            break;
        }

        std::vector<BYTE> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), bytesAvailable, &bytesRead)) {
            errorMessage = std::wstring(T(L"update.error.read_data_prefix")) + FormatWin32Error(GetLastError());
            readOk = false;
            break;
        }

        DWORD bytesWritten = 0;
        if (!WriteFile(fileHandle, buffer.data(), bytesRead, &bytesWritten, nullptr) || bytesWritten != bytesRead) {
            errorMessage = std::wstring(T(L"update.error.write_data_prefix")) + FormatWin32Error(GetLastError());
            readOk = false;
            break;
        }
    }

    CloseHandle(fileHandle);

    if (!readOk) {
        DeleteFileW(destinationPath.c_str());
        return false;
    }

    return true;
}

bool UpdateService::LaunchUpdaterProcess(DWORD currentProcessId,
                                         const std::wstring& downloadedExePath,
                                         const std::wstring& targetExePath,
                                         const std::wstring& expectedSha256,
                                         std::wstring& errorMessage) const {
    if (downloadedExePath.empty() || targetExePath.empty() || expectedSha256.empty()) {
        errorMessage = T(L"update.error.invalid_launch_params");
        return false;
    }
    if (!VerifyExpectedSha256(downloadedExePath, expectedSha256, errorMessage)) {
        return false;
    }
    std::wstringstream script;
    script << L"$pidToWait=" << currentProcessId << L";";
    script << L"$download='" << PowerShellUtils::EscapeSingleQuoted(downloadedExePath) << L"';";
    script << L"$target='" << PowerShellUtils::EscapeSingleQuoted(targetExePath) << L"';";
    script << L"$expected='" << PowerShellUtils::EscapeSingleQuoted(expectedSha256) << L"';";
    script << L"$backup=$target+'.bak';";
    script << L"while (Get-Process -Id $pidToWait -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 500 };";
    script << L"Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue;";
    script << L"$hadTarget=Test-Path -LiteralPath $target;";
    script << L"try {";
    script << L"if ($hadTarget) { Move-Item -LiteralPath $target -Destination $backup -Force };";
    script << L"Move-Item -LiteralPath $download -Destination $target -Force;";
    script << L"$sha=[System.Security.Cryptography.SHA256]::Create();";
    script << L"$stream=[System.IO.File]::Open($target,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,[System.IO.FileShare]::Read);";
    script << L"$actual=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant();";
    script << L"if ($actual -ne $expected) { throw 'Downloaded update checksum changed' };";
    script << L"$updated=Start-Process -FilePath $target -PassThru -ErrorAction Stop;";
    script << L"$stream.Dispose();$stream=$null;$sha.Dispose();$sha=$null;";
    script << L"Start-Sleep -Milliseconds 1000;";
    script << L"if ($updated.HasExited) { throw 'Updated process exited during startup' };";
    script << L"Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue;";
    script << L"} catch {";
    script << L"if ($stream) { $stream.Dispose() };if ($sha) { $sha.Dispose() };";
    script << L"Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue;";
    script << L"if ($hadTarget -and (Test-Path -LiteralPath $backup)) {";
    script << L"Move-Item -LiteralPath $backup -Destination $target -Force;";
    script << L"Start-Process -FilePath $target -ErrorAction SilentlyContinue";
    script << L"};";
    script << L"Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue;";
    script << L"exit 1;";
    script << L"};";

    std::wstring commandLine =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -Command \"" +
        script.str() + L"\"";

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    if (!CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo)) {
        errorMessage = std::wstring(T(L"update.error.updater_start_prefix"))
            + FormatWin32Error(GetLastError());
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

bool UpdateService::ResolveLatestReleaseTag(std::wstring& latestTag, std::wstring& errorMessage) const {
    latestTag.clear();

    WinHttpHandle session(WinHttpOpen(
        kUserAgent,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    ));
    if (!session) {
        errorMessage = std::wstring(T(L"update.error.winhttp_init_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }
    if (!SetWinHttpTimeouts(session.get())) {
        errorMessage = std::wstring(T(L"update.error.winhttp_init_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    WinHttpHandle connection(WinHttpConnect(session.get(), kGitHubHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        errorMessage = std::wstring(T(L"update.error.github_connect_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.get(),
        L"GET",
        kLatestReleasePath,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    ));
    if (!request) {
        errorMessage = std::wstring(T(L"update.error.http_request_create_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }
    if (!SetWinHttpTimeouts(request.get())) {
        errorMessage = std::wstring(T(L"update.error.http_request_create_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    if (!WinHttpSendRequest(
            request.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        errorMessage = std::wstring(T(L"update.error.send_check_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        errorMessage = std::wstring(T(L"update.error.receive_response_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
        errorMessage = std::wstring(T(L"update.error.http_status_prefix")) + FormatWin32Error(GetLastError());
        return false;
    }

    std::wstring sourceUrl;
    if (!QueryRequestOptionString(request.get(), WINHTTP_OPTION_URL, sourceUrl)) {
        QueryLocationHeader(request.get(), sourceUrl);
    }

    if (sourceUrl.empty()) {
        std::wstringstream stream;
        stream << T(L"update.error.latest_url_prefix")
               << statusCode << L")";
        errorMessage = stream.str();
        return false;
    }

    latestTag = ExtractTagFromUrl(sourceUrl);
    if (latestTag.empty()) {
        std::wstringstream stream;
        stream << T(L"update.error.extract_tag_prefix") << sourceUrl;
        errorMessage = stream.str();
        return false;
    }

    return true;
}

std::wstring UpdateService::NormalizeVersionFromTag(const std::wstring& rawTag) {
    if (!rawTag.empty() && (rawTag.front() == L'v' || rawTag.front() == L'V')) {
        return rawTag.substr(1);
    }
    return rawTag;
}

int UpdateService::CompareVersions(const std::wstring& left, const std::wstring& right) {
    const std::vector<std::wstring> leftParts = ParseVersionParts(left);
    const std::vector<std::wstring> rightParts = ParseVersionParts(right);
    const size_t maxParts = (std::max)(leftParts.size(), rightParts.size());

    for (size_t index = 0; index < maxParts; ++index) {
        const std::wstring leftValue = (index < leftParts.size()) ? leftParts[index] : L"0";
        const std::wstring rightValue = (index < rightParts.size()) ? rightParts[index] : L"0";
        if (leftValue.size() < rightValue.size()
            || (leftValue.size() == rightValue.size() && leftValue < rightValue)) {
            return -1;
        }
        if (leftValue.size() > rightValue.size()
            || (leftValue.size() == rightValue.size() && leftValue > rightValue)) {
            return 1;
        }
    }

    return 0;
}
