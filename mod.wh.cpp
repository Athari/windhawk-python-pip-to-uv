// ==WindhawkMod==
// @id              python-pip-to-uv
// @name            Replace Python Pip with UV
// @name:ru         Заменить Python Pip на UV
// @description     Replaces calls to os.system(python -m pip) with os.system(uv pip)
// @description:ru  Заменяет вызовы os.system(python -m pip) на os.system(uv pip)
// @version         0.1
// @author          Athari
// @github          https://github.com/Athari
// @twitter         https://twitter.com/Athari_P
// @homepage        https://github.com/Athari
// @include         python*.exe
// @include         pip*.exe
// @compilerOptions -lcomdlg32 -lole32 -lshlwapi -Wno-unqualified-std-cast-call
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Replace Python Pip with UV

Replaces calls to `os.system("python -m pip")` with `os.system("uv")`

# Getting started

* [Docs](https://github.com/ramensoftware/windhawk/wiki/Creating-a-new-mod)
* [Article](https://kylehalladay.com/blog/2020/11/13/Hooking-By-Example.html)
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- replacePip: true
  $name: Replace pip
  $description: When enabled, replaces calls to `pip` with `uv pip`
- replacePythonPip: true
  $name: Replace python pip
  $description: When enabled, replaces calls to `python -m pip` with `uv pip`
- replacePythonVenv: true
  $name: Replace python venv
  $description: When enabled, replaces calls to `python -m venv` with `uv venv`
- uvPath: ""
  $name: Uv path
  $description: When specified, calls uv from that path, otherwise relies on %PATH%
- uvCacheDir:
  - uvCacheDir: ""
    $name: Uv cache directory
    $description: When specified, sets path to uv cache directory, otherwise uses UV_CACHE_DIR
  - respectCacheDirArg: false
    $name: Respect pip arguments
    $description: When enabled, respects --cache-dir argument passed to pip
  $name: Uv cache directory
- uvLinkMode:
  - uvLinkMode: ""
    $options:
    - "": ""
    - clone: Clone (copy-on-write)
    - copy: Copy
    - hardlink: Hard link
    - symlink: Symlink
    $name: Uv link mode
    $description: When specified, sets method to use when installing packages from cache, otherwise uses UV_LINK_MODE
  - respectLinkModeArg: false
    $name: Respect venv arguments
    $description: When enabled, respects --symlinks and --copies arguments passed to venv
  $name: Uv link mode
*/
// ==/WindhawkModSettings==

#include <exception>
#include <cwchar>
#include <string>
#include <vector>
#include <unordered_map>

#include <processenv.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <winnt.h>
#include <winuser.h>
#include <wil/stl.h>
#include <wil/result_macros.h>
#include <wil/filesystem.h>
#include <wil/win32_helpers.h>

#include "windhawk_api.h"

using namespace std;
using namespace wil;

struct option_t {
    bool isSkipped = false;
    bool hasArg = false;
    bool isCommand = false;
    static const option_t simple;
    static const option_t withArg;
    static const option_t skip;
    static const option_t skipWithArg;
    static const option_t command;
};

const option_t option_t::simple      {};
const option_t option_t::withArg     { .hasArg = true };
const option_t option_t::skip        { .isSkipped = true };
const option_t option_t::skipWithArg { .isSkipped = true, .hasArg = true };
const option_t option_t::command     { .isCommand = true };

struct command_t {
    bool isSupported = true;
    bool hasLinkMode = false;
    static const command_t unsupported;
    static const command_t supported;
    static const command_t withLinkMode;
};

const command_t command_t::unsupported  { .isSupported = false };
const command_t command_t::supported    {};
const command_t command_t::withLinkMode { .hasLinkMode = true };

const auto pipOptions = unordered_map<wstring, option_t> {
    { L"--no-cache-dir",     option_t::skip },
    { L"--cache-dir",        option_t::skipWithArg },
    { L"--python",           option_t::skipWithArg },
    { L"--log",              option_t::withArg },
    { L"--keyring-provider", option_t::withArg },
    { L"--proxy",            option_t::withArg },
    { L"--retries",          option_t::withArg },
    { L"--timeout",          option_t::withArg },
    { L"--exists-action",    option_t::withArg },
    { L"--trusted-host",     option_t::withArg },
    { L"--cert",             option_t::withArg },
    { L"--client-cert",      option_t::withArg },
    { L"--use-feature",      option_t::withArg },
    { L"--use-deprecated",   option_t::withArg },
};

const auto pipCommands = unordered_map<wstring, command_t> {
    { L"install",    command_t::withLinkMode },
    { L"uninstall",  command_t::supported },
    { L"freeze",     command_t::supported },
    { L"list",       command_t::supported },
    { L"show",       command_t::supported },
    { L"check",      command_t::supported },
    { L"download",   command_t::unsupported }, // https://github.com/astral-sh/uv/issues/3163
    { L"wheel",      command_t::unsupported }, // https://github.com/astral-sh/uv/issues/1681
};

enum class command_name_t {
    unknown,
    pip,
    venv,
};

struct {
    wstring uvPath;
    wstring cmdPath;
    wstring envPath;
} sys;

struct {
    bool replacePip;
    bool replacePythonPip;
    bool replacePythonVenv;
    wstring uvPath;
    wstring uvCacheDir;
    bool respectCacheDirArg;
    wstring uvLinkMode;
    bool respectLinkModeArg;
} settings;

HRESULT translatePipArgs(const unique_hlocal_array_ptr<LPCWSTR>& args, UINT iArg, vector<wstring>& outArgs) {
    vector<wstring> otherArgs {};
    bool isCacheDirSet;
    wstring pipCommand;

    for (; iArg < args.size(); iArg++) {
        auto arg = args[iArg];
        if (wcscmp(arg, L"--cache-dir") == 0 && args.size() > iArg + 1 && settings.respectCacheDirArg) {
            outArgs.append_range(array { arg, args[iArg + 1] });
            isCacheDirSet = true;
            iArg++;
            continue;
        }

        auto opt = pipOptions.contains(arg) ? pipOptions.at(arg) : wcsncmp(arg, L"-", 1) == 0 ? option_t::simple : option_t::command;
        if (opt.isSkipped) {
            if (opt.hasArg)
                iArg++;
        } else if (!opt.isCommand) {
            if (opt.hasArg && args.size() > iArg + 1) {
                otherArgs.append_range(array { arg, args[iArg + 1] });
                iArg++;
            } else {
                otherArgs.push_back(arg);
            }
        } else {
            if (pipCommand.empty())
                pipCommand = arg;
            otherArgs.push_back(arg);
        }
    }

    auto cmd = pipCommands.contains(pipCommand) ? pipCommands.at(pipCommand) : command_t::unsupported;
    if (!cmd.isSupported)
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    if (!settings.uvCacheDir.empty() && !isCacheDirSet)
        outArgs.append_range(array { wstring(L"--cache-dir"), settings.uvCacheDir });
    if (!settings.uvLinkMode.empty() && cmd.hasLinkMode)
        outArgs.append_range(array { wstring(L"--link-mode"), settings.uvLinkMode });

    outArgs.append_range(otherArgs);
    return S_OK;
}

bool translateVenvArgs(const unique_hlocal_array_ptr<LPCWSTR>& args, UINT iArg, vector<wstring>& outArgs) {
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

using CreateProcessW_t = decltype(&CreateProcessW);
CreateProcessW_t CreateProcessW_Original;

wstring string_to_wstring(const string& s, UINT cp = CP_ACP)
{
    if (s.empty())
        return L"";
    int len = MultiByteToWideChar(cp, 0, s.c_str(), s.size(), nullptr, 0);
    wstring result(L'\0', len);
    MultiByteToWideChar(cp, 0, s.c_str(), s.size(), result.data(), len);
    return result;
}

LPCWSTR str_repr(LPCWSTR s) {
    return s != nullptr ? s : L"(null)";
}

wstring Wh_GetStdStringSetting(PCWSTR valueName) {
    auto buf = Wh_GetStringSetting(valueName);
    wstring result;
    result.append(buf);
    Wh_FreeStringSetting(buf);
    return result;
}

HRESULT CommandLineToArgvW(LPCWSTR lpCmdLine, unique_hlocal_array_ptr<LPCWSTR>& result) WI_NOEXCEPT {
    int numArgs = 0;
    auto lpCommandLineArgs = ::CommandLineToArgvW(lpCmdLine, &numArgs);
    RETURN_LAST_ERROR_IF_NULL(lpCommandLineArgs);
    result.reset(const_cast<LPCWSTR*>(lpCommandLineArgs), numArgs);
    return S_OK;
}

template<typename string_type, size_t length = 256>
HRESULT ExpandEnvAndSearchPath(PCWSTR input, PCWSTR extension, string_type& result) WI_NOEXCEPT
{
    unique_cotaskmem_string expandedName;
    RETURN_IF_FAILED((ExpandEnvironmentStringsW<string_type, length>(input, expandedName)));
    const HRESULT searchResult = (SearchPathW<string_type, length>(nullptr, expandedName.get(), extension, result));
    RETURN_HR_IF_EXPECTED(searchResult, searchResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    RETURN_IF_FAILED(searchResult);
    return S_OK;
}

template<typename Fn, typename Fail>
auto Attempt(const wstring& name, Fail&& fail, Fn&& fn) noexcept {
    try {
        return invoke(forward<Fn>(fn));
    } catch (const exception& ex) {
        Wh_Log(L"%s failed: %s", name.c_str(), string_to_wstring(ex.what()).c_str());
    } catch (...) {
        Wh_Log(L"%s failed: %s", name.c_str(), L"unknown exception");
    }
    return invoke(forward<Fail>(fail));
    // return invoke_result_t<F1>{};
}

void DoNothing() {}

BOOL WINAPI CreateProcessW_Hook(
    LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation
) {
    auto callOriginal = [&](LPCWSTR reason) {
        if (reason != nullptr)
            Wh_Log(L"-> CreateProcessW (/* original */) reason: %s", reason);
        return CreateProcessW_Original(
            lpApplicationName, lpCommandLine,
            lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
            lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
    };
    return Attempt(L"CreateProcessW_Hook", [&]() { return callOriginal(L"exception"); }, [&]() -> BOOL {
        Wh_Log(L"<- CreateProcessW (lpApplicationName = %s, lpCommandLine = %s, lpCurrentDirectory = %s)",
            str_repr(lpApplicationName), str_repr(lpCommandLine), str_repr(lpCurrentDirectory));

        // Parse command line into args

        if (lpCommandLine == nullptr || lpCommandLine[0] == 0)
            return callOriginal(L"empty command line");
        if (!settings.replacePip && !settings.replacePythonPip && !settings.replacePythonVenv)
            return callOriginal(L"disabled in settings");

        unique_hlocal_array_ptr<LPCWSTR> args;
        if (FAILED(CommandLineToArgvW(lpCommandLine, args)) || args.empty())
            return callOriginal(L"CommandLineToArgvW failed");
        if (args.size() <= 1)
            return callOriginal(L"command line too short");

        for (size_t i = 0; i < args.size(); i++)
            Wh_Log(L"  <- arg[%d] = %s", i, args[i]);

        // Find main exe path (skip cmd /c)

        auto currentDir = lpCurrentDirectory != nullptr ? wstring(lpCurrentDirectory) :  GetCurrentDirectoryW<wstring>();
        auto searchPath =  currentDir + L";" + sys.envPath;
        wstring exePath;
        // Wh_Log(LR"'(  SearchPathW("%s", "%s", "%s"))'", searchPath.c_str(), args[0], L".exe");
        if (FAILED(SearchPathW(searchPath.c_str(), args[0], L".exe", exePath)))
            return callOriginal(L"SearchPathW failed");

        auto exeName = PathFindFileNameW(exePath.c_str());
        auto iArg = 1u;
        auto command = command_name_t::unknown;

        if (wcsnicmp(exeName, L"cmd.exe", 7) == 0 && args.size() >= iArg + 2 && wcsnicmp(args[iArg], L"/c", 2) == 0) {
            iArg += 2;
            if (FAILED(SearchPathW(searchPath.c_str(), args[iArg + 1], L".exe", exePath)))
                return callOriginal(L"SearchPathW(cmd) failed");
            exeName = PathFindFileNameW(exePath.c_str());
        }

        Wh_Log(L"  exe = %s [?@%d]", exeName, iArg);

        // Find python command (pip/venv)

        wstring pythonName;
        if (wcsnicmp(exeName, L"pip", 3) == 0) {
            if (!settings.replacePip)
                return callOriginal(L"settings.replacePip = false");
            command = command_name_t::pip;
            iArg += 1;
        } else if (wcsnicmp(exeName, L"python", 6) == 0 && args.size() >= iArg + 2 && wcscmp(args[iArg], L"-m") == 0) {
            pythonName = exeName;
            if (wcscmp(args[iArg + 1], L"pip") == 0) {
                if (!settings.replacePythonPip)
                    return callOriginal(L"settings.replacePythonPip = false");
                command = command_name_t::pip;
                iArg += 2;
            } else if (wcscmp(args[iArg + 1], L"venv") == 0) {
                if (!settings.replacePythonVenv)
                    return callOriginal(L"settings.replacePythonVenv = false");
                command = command_name_t::venv;
                iArg += 2;
            }
        }

        if (command == command_name_t::unknown)
            return callOriginal(L"unrecognized command");

        wstring pythonPath;
        if (FAILED(SearchPathW(searchPath.c_str(), pythonName.c_str(), L".exe", pythonPath)))
            return callOriginal(L"SearchPathW(python) failed");

        Wh_Log(L"  python = %s [%d@%d]", pythonPath.c_str(), command, iArg);

        // Translate arguments

        vector<wstring> outArgs {
            sys.uvPath,
            L"--no-python-downloads",
            L"--python", pythonPath,
        };

        if (command == command_name_t::pip) {
            if (FAILED(translatePipArgs(args, iArg, outArgs)))
                return callOriginal(L"uv pip command not supported");
        } else if (command == command_name_t::venv) {
            if (FAILED(translateVenvArgs(args, iArg, outArgs)))
                return callOriginal(L"uv venv command not supported");
        }

        for (size_t i = 0; i < outArgs.size(); i++)
            Wh_Log(L"  -> arg[%d] = %s", i, outArgs.at(i).c_str());

        // Call original CreateProcessW with translated arguments

        auto newApplicationName = outArgs[0];
        auto newCommandLine = ArgvToCommandLine<vector<wstring>&, wchar_t>(outArgs);
        Wh_Log(L"-> CreateProcessW (lpApplicationName = %s, lpCommandLine = %s, lpCurrentDirectory = %s)",
            str_repr(newApplicationName.c_str()), str_repr(newCommandLine.data()), str_repr(lpCurrentDirectory));

        return CreateProcessW_Original(
            newApplicationName.c_str(), newCommandLine.data(),
            lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
            lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);

        // Always use https://github.com/microsoft/wil/wiki/Win32-helpers if possible

        // TODO
        // * Parse command line - use CommandLineToArgvW (then LocalFree)
        // * Skip "cmd.exe /c" (/c is case-insensitive) and "python.exe"
        // * Syntax of "-m pip" is case-sensitive
        // * Format command line using wil/ArgvToCommandLineW
        //    * Always add: --no-python-downloads, --python <PYTHON> (specify as full path; drop --python arg from source)

        // TODO pip
        // python -m pip <command> [options]
        // uv pip [OPTIONS] <COMMAND>
        // * has matches:
        // * depends on settings: --no-cache-dir, --cache-dir

        // TODO venv
        // python -m venv [-h] [--system-site-packages] [--symlinks | --copies] [--clear] [--upgrade] [--without-pip] [--prompt PROMPT] [--upgrade-deps] ENV_DIR [ENV_DIR ...]
        // uv venv [OPTIONS] [NAME]
        // * exact matches: --prompt, --system-site-packages
        // * depends on settings: --symlinks, --copies

        // lpCommandLine:
        // C:\WINDOWS\system32\cmd.exe /c python -m pip --version
        // C:\Apps\Python\Python312\python.exe -m pip --version
        // C:\WINDOWS\system32\cmd.exe /c python -m pip --version
        // C:\Apps\Python\Python312\python.exe -m pip --version
        // "C:\Apps\Python\Python312\python.exe" "C:\Apps\Python\Python312\Scripts\pip.exe" --version
        // "C:\Apps\Python\Python312\Scripts\pip.exe" --version
    });
}

void LoadSettings() {
    settings.replacePip = Wh_GetIntSetting(L"replacePip");
    settings.replacePythonPip = Wh_GetIntSetting(L"replacePythonPip");
    settings.replacePythonVenv = Wh_GetIntSetting(L"replacePythonVenv");
    settings.uvPath = Wh_GetStdStringSetting(L"uvPath");
    settings.uvCacheDir = Wh_GetStdStringSetting(L"uvCacheDir");
    settings.respectCacheDirArg = Wh_GetIntSetting(L"respectCacheDirArg");
    settings.uvLinkMode = Wh_GetStdStringSetting(L"uvLinkMode");
    settings.respectLinkModeArg = Wh_GetIntSetting(L"respectLinkModeArg");

    sys.cmdPath = GetEnvironmentVariableW<wstring>(L"ComSpec");
    sys.envPath = GetEnvironmentVariableW<wstring>(L"PATH");

    if (settings.uvPath.empty()) {
        THROW_IF_FAILED(SearchPathW(sys.envPath.c_str(), L"uv.exe", nullptr, sys.uvPath));
    } else {
        auto expandedUvPath = ExpandEnvironmentStringsW(settings.uvPath.c_str());
        auto attr = GetFileAttributes(expandedUvPath.get());
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
            THROW_IF_FAILED(SearchPathW(expandedUvPath.get(), L"uv.exe", nullptr, sys.uvPath));
        else
            THROW_IF_FAILED(SearchPathW(sys.envPath.c_str(), expandedUvPath.get(), L".exe", sys.uvPath));
    }

    // Wh_Log(L"ComSpec = %s", sys.cmdPath.c_str());
    // Wh_Log(L"PATH = %s", sys.envPath.c_str());
    Wh_Log(L"UV = %s", sys.uvPath.c_str());
}

BOOL Wh_ModInit() {
    return Attempt(L"ModInit", []() { return FALSE; }, []() {
        Wh_Log(L"ModInit in %s", GetModuleFileNameW().get());

        LoadSettings();

        Wh_SetFunctionHook((void*)CreateProcessW, (void*)CreateProcessW_Hook, (void**)&CreateProcessW_Original);

        SetResultLoggingCallback([](const FailureInfo& failure) __stdcall noexcept {
            const size_t wilLogMessageSize = 2048;
            wchar_t logMessage[wilLogMessageSize];
            if (SUCCEEDED(GetFailureLogString(logMessage, wilLogMessageSize, failure)))
                Wh_Log(L"[wil] %s", logMessage);
        });
        return TRUE;
    });
}

void Wh_ModUninit() {
    Attempt(L"ModUninit", DoNothing, []() {
        Wh_Log(L"ModUninit");
    });
}

void Wh_ModSettingsChanged() {
    Attempt(L"ModSettingsChanged", DoNothing, []() {
        Wh_Log(L"ModSettingsChanged");
        LoadSettings();
    });
}