// Single-file distribution: Windows Cabinet API, no Qt or external unpacker.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <fdi.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
struct Stream {
    HANDLE file = INVALID_HANDLE_VALUE;
    size_t offset = 0;
    bool memory = false;
    ~Stream() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};
struct Bundle {
    const BYTE *data = nullptr;
    DWORD size = 0;
    fs::path root;
    std::vector<std::unique_ptr<Stream>> streams;
    std::vector<fs::path> files;
    std::vector<fs::path> directories;
    ~Bundle() {
        streams.clear();
        // Only remove paths created by this instance; no recursive directory deletion.
        for (int attempt = 0; attempt < 5 && !root.empty(); ++attempt) {
            for (const auto &file : files) DeleteFileW(file.c_str());
            for (auto it = directories.rbegin(); it != directories.rend(); ++it)
                RemoveDirectoryW(it->c_str());
            if (RemoveDirectoryW(root.c_str()) || GetLastError() == ERROR_PATH_NOT_FOUND) break;
            Sleep(100);
        }
    }
};
Bundle *bundle = nullptr; // FDI I/O callbacks are synchronous, in this one process.

FNALLOC(allocate) { return std::malloc(cb); }
FNFREE(release) { std::free(pv); }
FNOPEN(openCabinet) {
    (void)oflag; (void)pmode;
    if (std::strcmp(pszFile, "payload.cab") != 0) return -1;
    auto stream = std::make_unique<Stream>();
    stream->memory = true;
    const auto id = reinterpret_cast<INT_PTR>(stream.get());
    bundle->streams.push_back(std::move(stream));
    return id;
}
FNREAD(readCabinet) {
    auto &stream = *reinterpret_cast<Stream *>(hf);
    if (!stream.memory || stream.offset > bundle->size) return UINT(-1);
    const size_t count = std::min<size_t>(cb, bundle->size - stream.offset);
    std::memcpy(pv, bundle->data + stream.offset, count);
    stream.offset += count;
    return static_cast<UINT>(count);
}
FNWRITE(writeFile) {
    auto &stream = *reinterpret_cast<Stream *>(hf);
    DWORD written = 0;
    return WriteFile(stream.file, pv, cb, &written, nullptr) ? written : UINT(-1);
}
FNCLOSE(closeStream) {
    auto &stream = *reinterpret_cast<Stream *>(hf);
    if (stream.file != INVALID_HANDLE_VALUE) {
        const BOOL ok = CloseHandle(stream.file);
        stream.file = INVALID_HANDLE_VALUE;
        return ok ? 0 : -1;
    }
    return 0;
}
FNSEEK(seekCabinet) {
    auto &stream = *reinterpret_cast<Stream *>(hf);
    if (!stream.memory) return -1;
    const long long base = seektype == SEEK_SET ? 0 : seektype == SEEK_CUR
        ? static_cast<long long>(stream.offset) : seektype == SEEK_END ? bundle->size : -1;
    const long long position = base + dist;
    if (base < 0 || position < 0 || position > bundle->size) return -1;
    stream.offset = static_cast<size_t>(position);
    return static_cast<long>(position);
}

bool safeRelativePath(const char *name, fs::path &relative)
{
    // Packaged Qt filenames are ASCII. Reject absolute paths, ADS and traversal.
    if (!name || !*name) return false;
    std::wstring wide;
    for (const unsigned char c : std::string(name)) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+' || c == '.' || c == '\\' || c == '/'))
            return false;
        wide.push_back(c == '/' ? L'\\' : c);
    }
    relative = fs::path(wide);
    if (relative.is_absolute() || relative.has_root_path()) return false;
    for (const auto &part : relative)
        if (part.empty() || part == L"." || part == L".." || part.native().back() == L'.') return false;
    return true;
}

FNFDINOTIFY(extractFile)
{
    try {
        if (fdint == fdintNEXT_CABINET || fdint == fdintPARTIAL_FILE) return -1;
        if (fdint == fdintCLOSE_FILE_INFO) return closeStream(pfdin->hf) == 0;
        if (fdint != fdintCOPY_FILE) return 0;
        fs::path relative;
        if (!safeRelativePath(pfdin->psz1, relative)) return -1;
        fs::path parent = bundle->root;
        for (const auto &part : relative.parent_path()) {
            parent /= part;
            if (CreateDirectoryW(parent.c_str(), nullptr)) bundle->directories.push_back(parent);
            else {
                const DWORD attributes = GetFileAttributesW(parent.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return -1;
            }
        }
        const fs::path path = bundle->root / relative;
        auto stream = std::make_unique<Stream>();
        stream->file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (stream->file == INVALID_HANDLE_VALUE) return -1;
        bundle->files.push_back(path);
        const auto id = reinterpret_cast<INT_PTR>(stream.get());
        bundle->streams.push_back(std::move(stream));
        return id;
    } catch (...) { return -1; }
}

bool createPrivateDirectory(Bundle &state)
{
    std::vector<wchar_t> temp(32768);
    const DWORD size = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (!size || size >= temp.size()) return false;
    GUID guid{};
    wchar_t name[40]{};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, name, 40)) return false;
    const fs::path candidate = fs::path(temp.data()) / (std::wstring(L"EasySerial-") + name);
    if (!CreateDirectoryW(candidate.c_str(), nullptr)) return false;
    state.root = candidate; // Set only after we create it, never adopt an existing directory.
    return true;
}

int run(HINSTANCE instance, const wchar_t *arguments, bool silent)
{
    auto fail = [silent](const wchar_t *message, int code) {
        if (!silent) MessageBoxW(nullptr, message, L"EasySerial", MB_OK | MB_ICONERROR);
        return code;
    };
    Bundle state;
    bundle = &state;
    const HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!resource) return fail(L"The application bundle is missing.", 10);
    state.size = SizeofResource(instance, resource);
    state.data = static_cast<const BYTE *>(LockResource(LoadResource(instance, resource)));
    if (!state.data || !state.size) return fail(L"The application bundle is invalid.", 11);
    if (!createPrivateDirectory(state))
        return fail(L"Cannot create a temporary folder. Check your TEMP folder and available disk space.", 12);
    ERF error{};
    HFDI cabinet = FDICreate(allocate, release, openCabinet, readCabinet, writeFile,
                             closeStream, seekCabinet, cpuUNKNOWN, &error);
    if (!cabinet) return fail(L"Cannot initialize Windows Cabinet decompression.", 13);
    char filename[] = "payload.cab";
    char path[] = "";
    const BOOL extracted = FDICopy(cabinet, filename, path, 0, extractFile, nullptr, nullptr);
    FDIDestroy(cabinet);
    state.streams.clear();
    if (!extracted) return fail(L"Cannot unpack EasySerial. Check available disk space or download the file again.", 14);

    const auto executable = state.root / L"EasySerial.exe";
    std::wstring command = L"\"" + executable.wstring() + L"\" ";
    command += arguments; // Preserve Windows quoting when forwarding arguments.
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // The unpacked application remains dynamically linked to replaceable Qt DLLs.
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                         nullptr, state.root.c_str(), &startup, &process))
        return fail(L"Cannot start EasySerial. Check antivirus quarantine or Windows compatibility.", 15);
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (exitCode && !silent)
        MessageBoxW(nullptr, L"EasySerial exited unexpectedly. Please retry or rebuild the application bundle.",
                    L"EasySerial", MB_OK | MB_ICONERROR);
    return static_cast<int>(exitCode);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int)
{
    // Do not search the working directory for the launcher's own DLLs.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silent = false;
    for (int i = 1; argv && i < argc; ++i)
        if (std::wstring(argv[i]) == L"--smoke-test") silent = true;
    LocalFree(argv);
    try { return run(instance, arguments, silent); }
    catch (...) {
        if (!silent) MessageBoxW(nullptr, L"Cannot launch EasySerial due to a packaging or file error.",
                                L"EasySerial", MB_OK | MB_ICONERROR);
        return 16;
    }
}
