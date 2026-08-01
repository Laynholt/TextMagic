#include "LogFile.h"

#include "EncodingUtils.h"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace LogFile {
bool Append(const std::wstring& path, const std::wstring& line) {
    std::error_code sizeError;
    const bool empty = !std::filesystem::exists(path, sizeError)
        || std::filesystem::file_size(path, sizeError) == 0;
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::app);
    if (!file) {
        return false;
    }

    if (empty) {
        constexpr char bom[] = "\xEF\xBB\xBF";
        file.write(bom, 3);
    } else {
        file.write("\r\n", 2);
    }
    const std::string utf8 = EncodingUtils::WideToUtf8(line);
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
    return std::ofstream(std::filesystem::path(path), std::ios::binary | std::ios::trunc).good();
}
}
