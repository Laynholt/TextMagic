#include "LogFile.h"

#include "EncodingUtils.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace LogFile {
bool Append(const std::wstring& path,
            const std::wstring& line,
            std::uintmax_t maxBytes) {
    const std::filesystem::path logPath(path);
    std::error_code sizeError;
    std::uintmax_t currentSize = std::filesystem::exists(logPath, sizeError)
        ? std::filesystem::file_size(logPath, sizeError)
        : 0;
    if (sizeError) {
        currentSize = 0;
    }

    const std::string utf8 = EncodingUtils::WideToUtf8(line);
    const std::uintmax_t appendBytes = utf8.size() + (currentSize == 0 ? 3 : 2);
    if (maxBytes > 0 && currentSize > 0
        && (appendBytes > maxBytes || currentSize > maxBytes - appendBytes)) {
        const std::filesystem::path backupPath = logPath.wstring() + L".old";
        std::error_code rotateError;
        std::filesystem::remove(backupPath, rotateError);
        if (rotateError) {
            return false;
        }
        std::filesystem::rename(logPath, backupPath, rotateError);
        if (rotateError) {
            return false;
        }
        currentSize = 0;
    }

    const bool empty = currentSize == 0;
    std::ofstream file(logPath, std::ios::binary | std::ios::app);
    if (!file) {
        return false;
    }

    if (empty) {
        constexpr char bom[] = "\xEF\xBB\xBF";
        file.write(bom, 3);
    } else {
        file.write("\r\n", 2);
    }
    file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return file.good();
}

std::wstring Read(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return {};
    }
    const std::string bytes{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    };
    return EncodingUtils::Utf8ToWide(bytes, true, false);
}

bool Clear(const std::wstring& path) {
    const bool cleared = std::ofstream(
        std::filesystem::path(path),
        std::ios::binary | std::ios::trunc
    ).good();
    std::error_code removeError;
    std::filesystem::remove(std::filesystem::path(path).wstring() + L".old", removeError);
    return cleared && !removeError;
}
}
