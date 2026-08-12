#include "FileSystemUtils.h"

namespace FileSystemUtils {

namespace {
template <typename Query>
std::wstring GetGrowingBufferPath(
    Query query,
    std::error_code* error
) {
    if (!error) {
        return {};
    }
    error->clear();

    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = query(buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            *error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
            return {};
        }
        if (length < buffer.size() - 1) {
            return std::wstring(buffer.data(), length);
        }
        if (buffer.size() >= 32768) {
            *error = std::make_error_code(std::errc::filename_too_long);
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
}
}

bool CollectRegularFiles(
    const std::filesystem::path& root,
    bool recursive,
    std::vector<std::filesystem::path>* files,
    std::error_code* error
) {
    if (!files || !error) {
        return false;
    }

    files->clear();
    error->clear();

    if (recursive) {
        std::filesystem::recursive_directory_iterator iterator(root, *error);
        const std::filesystem::recursive_directory_iterator end;
        while (!*error && iterator != end) {
            std::error_code entryError;
            if (iterator->is_regular_file(entryError)) {
                files->push_back(iterator->path());
            }
            if (entryError) {
                *error = entryError;
                return false;
            }
            iterator.increment(*error);
        }
        return !*error;
    }

    std::filesystem::directory_iterator iterator(root, *error);
    const std::filesystem::directory_iterator end;
    while (!*error && iterator != end) {
        std::error_code entryError;
        if (iterator->is_regular_file(entryError)) {
            files->push_back(iterator->path());
        }
        if (entryError) {
            *error = entryError;
            return false;
        }
        iterator.increment(*error);
    }
    return !*error;
}

std::wstring GetTempDirectory(std::error_code* error) {
    return GetGrowingBufferPath(
        [](wchar_t* buffer, DWORD size) { return GetTempPathW(size, buffer); },
        error);
}

std::wstring GetModulePath(HMODULE module, std::error_code* error) {
    return GetGrowingBufferPath(
        [module](wchar_t* buffer, DWORD size) {
            return GetModuleFileNameW(module, buffer, size);
        },
        error);
}

} // namespace FileSystemUtils
