// ==WindhawkMod==
// @id              python-pip-to-uv
// @name            Replace Python Pip with UV
// @name:ru         Заменить Python Pip на UV
// @description     Replaces calls to os.system("python -m pip") with os.system("uv pip")
// @description:ru  Заменяет вызовы os.system("python -m pip") на os.system("uv pip")
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

// MARK: readme.md

// ==WindhawkModReadme==
/*
# Replace Python Pip with UV

Replaces calls to `os.system("python -m pip")` with `os.system("uv pip")` and the like by hooking `CreateProcessW` in `python*.exe` and `pip*.exe`.

The primary purpose of this mod is to enforce uv's caching and performance onto every script and app that run on the system, no matter what the developers of those scripts and apps think about the unfathomable complexity of migrating to uv and your desire to nuke pip from high orbit.

## Features

* Handles all ways of running pip: `[cmd[.exe] /c]` { `pip[.exe]` | `python[.exe]` { `-m pip` | `scripts\pip[.exe]` } }.
* Handles all ways of running venv: `[cmd[.exe] /c] python[.exe]` { `-m venv` | `scripts\venv[.exe]` }.
* Handles all ways of running virtualenv: `[cmd[.exe] /c]` { `virtualenv[.exe]` | `python[.exe]` { `-m virtualenv` | `scripts\virtualenv[.exe]` } }.
* Handles all modules which end up calling `CreateProcessW`: `os.system`, `subprocess.run` etc.
* Handles all python processes, including installed manually, managed by uv, bundled with electron apps etc.
* Handles `cache-dir` and `link-mode` options of `uv` and allows overriding them via settings and/or arguments.

The mod *does not* handle `pip` any better than `uv` — it just blindly replaces arguments, as long as the `uv pip` subcommand is supported. Unsupported subcommands fall back to running `pip`. Notably, these include `download` and `wheel`, which haven't been implemented in `uv` yet.

Translation of `venv` and `virtualenv` to `uv venv` is more precise — the mod skips all unsupported options.

The mod restricts smart handling of python versions uv with `--no-python-downloads` and `--python-preference only-system` arguments to reflect how pre-uv commands function.

## Configuration

* The mod will try to use Python-specific `uv` when available, but will fall back to global `uv`, if it exists. Hint: run `winget install astral-sh.uv`.
* If you like living on the edge, you can add `cmd.exe`, `pwsh.exe` and `powershell.exe` to the list of included processes. This way, even if you type `pip install` into your favorite terminal, the mod will translate the command for you. However, if your system ends up crashing because it can't run something due to a bug in the mod, don't tell me I haven't warned you.

## Troubleshooting

* If something goes wrong, enable trace logs in the mod settings. It'll tell you what executables get resolved to what, what paths are searched and how arguments were transformed.
* If you end up with 500 python processes due to a bug in the mod, the command line you're looking for is `taskkill /im python.exe /f /t` (you may need to run it a few times in rapid sucession). I haven't seen that bug in a while, but just in case.
*/
// ==/WindhawkModReadme==

// MARK: config.yaml

// ==WindhawkModSettings==
/*
- replacePip: true
  $name: Replace pip
  $description: When enabled, replaces calls to `pip`, `python -m pip`, `python scripts/pip` with `uv pip`
- replaceVenv: true
  $name: Replace venv
  $description: When enabled, replaces calls to `python -m venv` with `uv venv`
- replaceVirtualEnv: true
  $name: Replace virtualenv
  $description: When enabled, replaces calls to `virtualenv`, `python -m virtualenv`, `python scripts/virtualenv` with `uv venv`
- uvPath: ""
  $name: Uv path
  $description: When specified and search for python-specific uv fails, calls uv from that path, otherwise relies on %PATH%
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
- debug:
  - logLevel: info
    $options:
    - error: Error
    - warn: Warning
    - info: Info
    - debug: Debug
    - trace: Trace
    $name: Log level
    $description: Detail level of logging
  $name: Debug
*/
// ==/WindhawkModSettings==

// MARK: #include

#pragma clang diagnostic ignored "-Wunqualified-std-cast-call" // WHY U NO WORK TIDY CONFIG???

#include <exception>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_map>

#include <errhandlingapi.h>
#include <processenv.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <windows.h>
#include <winnt.h>
#include <winuser.h>

#include <wil/stl.h>
#include <wil/result_macros.h>
#include <wil/filesystem.h>
#include <wil/win32_helpers.h>

#include <windhawk_api.h>

using namespace std;
using namespace wil;

const size_t MAX_LOG_STR = 160;

// MARK: utils.h

wstring string_to_wstring(const string& s, UINT cp = CP_ACP)
{
    if (s.empty())
        return L"";
    int len = MultiByteToWideChar(cp, 0, s.c_str(), s.size(), nullptr, 0);
    wstring result(len, L'\0');
    MultiByteToWideChar(cp, 0, s.c_str(), s.size(), result.data(), len);
    return result;
}

bool wcsempty(LPCWSTR s) { return s == nullptr || s[0] == '\0'; }
bool wcsequals(LPCWSTR s1, LPCWSTR s2) { return wcscmp(s1, s2) == 0; }
bool wcsiequals(LPCWSTR s1, LPCWSTR s2) { return wcsicmp(s1, s2) == 0; }
bool wcsstarts(LPCWSTR s, LPCWSTR with) {
    size_t wlen = wcslen(with);
    return wcslen(s) >= wlen && wcsncmp(s, with, wlen) == 0;
}
bool wcsistarts(LPCWSTR s, LPCWSTR with) {
    size_t wlen = wcslen(with);
    return wcslen(s) >= wlen && wcsnicmp(s, with, wlen) == 0;
}
bool wcsends(LPCWSTR s, LPCWSTR with) {
    size_t slen = wcslen(s), wlen = wcslen(with);
    return slen >= wlen && wcsncmp(s + slen - wlen, with, wlen) == 0;
}
bool wcsiends(LPCWSTR s, LPCWSTR with) {
    size_t slen = wcslen(s), wlen = wcslen(with);
    return slen >= wlen && wcsnicmp(s + slen - wlen, with, wlen) == 0;
}

LPCWSTR repr(LPCWSTR s) { return s != nullptr ? s : L"(null)"; }

wstring Wh_GetStdStringSetting(PCWSTR valueName) {
    auto buf = Wh_GetStringSetting(valueName);
    wstring result;
    result.append(buf);
    Wh_FreeStringSetting(buf);
    return result;
}

bool Wh_GetBoolSetting(PCWSTR valueName) {
    return Wh_GetIntSetting(valueName) != 0;
}

wstring PathGetDirNameW(LPCWSTR path) {
    wstring dir = path;
    PathRemoveFileSpecW(dir.data());
    dir.resize(wcslen(dir.c_str()));
    return dir;
}

HRESULT CommandLineToArgvW(LPCWSTR lpCmdLine, unique_hlocal_array_ptr<LPCWSTR>& result) WI_NOEXCEPT {
    int numArgs = 0;
    auto lpCommandLineArgs = ::CommandLineToArgvW(lpCmdLine, &numArgs);
    RETURN_LAST_ERROR_IF_NULL(lpCommandLineArgs);
    result.reset(const_cast<LPCWSTR*>(lpCommandLineArgs), numArgs);
    return S_OK;
}

template<typename string_type, size_t length = MAX_PATH>
HRESULT ExpandEnvAndSearchPathW(PCWSTR path, PCWSTR fileName, PCWSTR extension, string_type& result) WI_NOEXCEPT
{
    wstring expandedName;
    RETURN_IF_FAILED((ExpandEnvironmentStringsW<string_type, length>(fileName, expandedName)));
    const HRESULT searchResult = (SearchPathW<string_type, length>(path, expandedName.c_str(), extension, result));
    RETURN_HR_IF_EXPECTED(searchResult, searchResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    RETURN_IF_FAILED(searchResult);
    return S_OK;
}

// MARK: options.h

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
    bool isSupported = true;  // uv has matching command
    bool hasLinkMode = false; // --link-mode <LINK_MODE>
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

const auto venvOptions = unordered_map<wstring, option_t> {
    { L"--clear",                option_t::skip },
    { L"--upgrade",              option_t::skip },
    { L"--upgrade-deps",         option_t::skip },
    { L"--symlinks",             option_t::skip },
    { L"--copies",               option_t::skip },
    { L"--without-pip",          option_t::skip },
    { L"--prompt",               option_t::withArg },
};

const auto virtualenvOptions = unordered_map<wstring, option_t> {
    { L"--clear",                option_t::skip },
    { L"--no-vcs-ignore",        option_t::skip },
    { L"--no-download",          option_t::skip },
    { L"--never-download",       option_t::skip },
    { L"--symlinks",             option_t::skip },
    { L"--copies",               option_t::skip },
    { L"--no-seed",              option_t::skip },
    { L"--without-pip",          option_t::skip },
    { L"--seeder",               option_t::skipWithArg },
    { L"--creator",              option_t::skipWithArg },
    { L"--activators",           option_t::skipWithArg },
    { L"--prompt",               option_t::withArg },
    { L"--python",               option_t::withArg },
    { L"-p",                     option_t::withArg },
};

enum class command_name_t {
    unknown,
    pip,
    venv,
    virtualenv,
};

enum class log_level_t {
    error,
    warn,
    info,
    debug,
    trace,
};

const auto logLevels = unordered_map<wstring, log_level_t> {
    { L"error", log_level_t::error },
    { L"warn",  log_level_t::warn },
    { L"info",  log_level_t::info },
    { L"debug", log_level_t::debug },
    { L"trace", log_level_t::trace },
};

// MARK: settings.h

struct {
    wstring defaultUvPath;
} sys;

struct {
    bool replacePip = true;
    bool replaceVenv = true;
    bool replaceVirtualEnv = true;
    wstring uvPath;
    wstring uvCacheDir;
    bool respectCacheDirArg = false;
    wstring uvLinkMode;
    bool respectLinkModeArg = false;
    log_level_t logLevel = log_level_t::info;
} settings;

// MARK: mod.cpp

template<typename Fn, typename Fail>
requires is_nothrow_invocable_v<Fail>
auto Attempt(const wstring& name, Fail&& fail, Fn&& fn) noexcept {
    try {
        return invoke(forward<Fn>(fn));
    } catch (const exception& ex) {
        if (settings.logLevel >= log_level_t::error)
            Wh_Log(L"%s failed: %s", name.c_str(), string_to_wstring(ex.what()).c_str());
    } catch (...) {
        if (settings.logLevel >= log_level_t::error)
            Wh_Log(L"%s failed: %s", name.c_str(), L"unknown exception");
    }
    return invoke(forward<Fail>(fail));
}

void DoNothing() noexcept {}

HRESULT translatePipArgs(const unique_hlocal_array_ptr<LPCWSTR>& args, UINT iArg, const wstring& pythonPath, vector<wstring>& outArgs) {
    vector<wstring> otherArgs {};
    auto isCacheDirSet = false;
    wstring pipCommand;

    for (; iArg < args.size(); iArg++) {
        auto arg = args[iArg];
        auto opt = pipOptions.contains(arg) ? pipOptions.at(arg) : wcsstarts(arg, L"-") ? option_t::simple : option_t::command;
        auto hasNextArg = args.size() > iArg + 1;

        if (wcsequals(arg, L"--cache-dir") && hasNextArg && settings.respectCacheDirArg) {
            outArgs.append_range(array { arg, args[iArg + 1] });
            isCacheDirSet = true;
            iArg++;
        } else if (opt.isSkipped) {
            if (opt.hasArg)
                iArg++;
        } else if (!opt.isCommand) {
            if (opt.hasArg && hasNextArg) {
                otherArgs.append_range(array { arg, args[iArg + 1] });
                iArg++;
            } else {
                otherArgs.emplace_back(arg);
            }
        } else {
            if (pipCommand.empty())
                pipCommand = arg;
            otherArgs.emplace_back(arg);
        }
    }

    auto cmd = pipCommands.contains(pipCommand) ? pipCommands.at(pipCommand) : pipCommand.empty() ? command_t::supported : command_t::unsupported;
    if (!cmd.isSupported)
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

    if (!settings.uvCacheDir.empty() && !isCacheDirSet)
        outArgs.append_range(array { wstring(L"--cache-dir"), settings.uvCacheDir });

    outArgs.emplace_back(L"pip");
    outArgs.append_range(otherArgs);

    if (!pipCommand.empty())
        outArgs.append_range(array { wstring(L"--python"), pythonPath });
    if (!settings.uvLinkMode.empty() && cmd.hasLinkMode)
        outArgs.append_range(array { wstring(L"--link-mode"), settings.uvLinkMode });

    return S_OK;
}

HRESULT translateVenvArgs(const unordered_map<wstring, option_t>& options,
    const unique_hlocal_array_ptr<LPCWSTR>& args, UINT iArg, const wstring& pythonPath, vector<wstring>& outArgs
) {
    vector<wstring> otherArgs {};
    wstring linkMode = settings.uvLinkMode;
    bool hasSeed = true;
    wstring envDir;

    for (; iArg < args.size(); iArg++) {
        auto arg = args[iArg];
        auto opt = options.contains(arg) ? options.at(arg) : wcsstarts(arg, L"-") ? option_t::simple : option_t::command;
        auto hasNextArg = args.size() > iArg + 1;

        if (wcsequals(arg, L"--symlinks") && settings.respectLinkModeArg) {
            linkMode = L"symlink";
        } else if (wcsequals(arg, L"--copies") && settings.respectLinkModeArg) {
            linkMode = L"copy";
        } else if (wcsequals(arg, L"--no-seed") || wcsequals(arg, L"--without-pip")) {
            hasSeed = false;
        } else if (opt.isSkipped) {
            if (opt.hasArg)
                iArg++;
        } else if (!opt.isCommand) {
            if (opt.hasArg && hasNextArg) {
                otherArgs.append_range(array { arg, args[iArg + 1] });
                iArg++;
            } else {
                otherArgs.emplace_back(arg);
            }
        } else {
            if (envDir.empty())
                envDir = arg;
            otherArgs.emplace_back(arg);
        }
    }

    outArgs.emplace_back(L"venv");
    if (!envDir.empty())
        outArgs.emplace_back(envDir);

    outArgs.append_range(otherArgs);
    outArgs.append_range(array { wstring(L"--python"), pythonPath });

    if (!linkMode.empty())
        outArgs.append_range(array { wstring(L"--link-mode"), linkMode });
    if (hasSeed)
        outArgs.emplace_back(L"--seed");

    return S_OK;
}

// MARK: CreateProcessW

using CreateProcessW_t = decltype(&CreateProcessW);
CreateProcessW_t CreateProcessW_Original;

BOOL WINAPI CreateProcessW_Hook(
    LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation
) {
    auto bypassed = false;

    // auto fail = [&]() {
    //     SetLastError(ERROR_NOT_SUPPORTED);
    //     return FALSE;
    // };

    auto bypass = [&](LPCWSTR reason) noexcept -> BOOL {
        if (bypassed)
            return FALSE;
        bypassed = true;

        if (settings.logLevel >= log_level_t::info)
            Wh_Log(L"-> CreateProcessW (/* original */) reason: %s", reason);
        // return fail();
        return CreateProcessW_Original(
            lpApplicationName, lpCommandLine,
            lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
            lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
    };

    return Attempt(L"CreateProcessW_Hook", [&]() noexcept { return bypass(L"exception"); }, [&]() -> BOOL {
        // Init

        if (settings.logLevel >= log_level_t::info)
            Wh_Log(L"<- CreateProcessW (\"%s\", \"%s\", lpCurrentDirectory = \"%s\")",
                repr(lpApplicationName), repr(lpCommandLine), repr(lpCurrentDirectory));

        if (!settings.replacePip && !settings.replaceVenv && !settings.replaceVirtualEnv)
            return bypass(L"disabled in settings");

        // Parse command line into args

        auto commandLine = !wcsempty(lpCommandLine) ? lpCommandLine : lpApplicationName;
        unique_hlocal_array_ptr<LPCWSTR> args;
        if (FAILED(CommandLineToArgvW(commandLine, args)) || args.empty())
            return bypass(format(L"CommandLineToArgvW({}) failed", commandLine).c_str());

        if (settings.logLevel >= log_level_t::trace)
            for (size_t i = 0; i < args.size(); i++)
                Wh_Log(L"  <- arg[%d] = %s", i, args[i]);

        // Find main exe path (skip cmd /c)

        auto envPath = GetEnvironmentVariableW<wstring>(L"PATH");
        auto currentDir = !wcsempty(lpCurrentDirectory) ? lpCurrentDirectory :  GetCurrentDirectoryW<wstring>();
        auto searchPath =  currentDir + L";" + envPath;

        if (settings.logLevel >= log_level_t::trace)
            Wh_Log(L"  path = %s...", searchPath.substr(0, min(MAX_LOG_STR, searchPath.size())).c_str());

        wstring exePath;
        if (FAILED(ExpandEnvAndSearchPathW(searchPath.c_str(), args[0], L".exe", exePath)))
            return bypass(format(L"ExpandEnvAndSearchPathW({}) failed", args[0]).c_str());

        auto exeName = PathFindFileNameW(exePath.c_str());
        auto iArg = 1U;
        auto command = command_name_t::unknown;

        if (wcsistarts(exeName, L"cmd.exe") && args.size() > iArg + 1 && wcsistarts(args[iArg], L"/c")) {
            if (args.size() == iArg + 2) {
                // cmd.exe /c "... ..."
                if (FAILED(CommandLineToArgvW(args[iArg + 1], args)) || args.empty())
                    return bypass(format(L"CommandLineToArgvW({}) failed", args[iArg + 1]).c_str());

                iArg = 0;
            } else {
                // cmd.exe /c ...
                iArg++;
            }

            // Wh_Log(LR"'(  SearchPathW("%s", "%s", "%s"))'", searchPath.substr(0, 256).c_str(), args[iArg + 1], L".exe");
            if (FAILED(ExpandEnvAndSearchPathW(searchPath.c_str(), args[iArg], L".exe", exePath)))
                return bypass(format(L"ExpandEnvAndSearchPathW({}) failed", args[iArg]).c_str());

            exeName = PathFindFileNameW(exePath.c_str());
            iArg++;
        }

        if (settings.logLevel >= log_level_t::trace)
            Wh_Log(L"  exe = %s [?@%d]", exePath.c_str(), iArg);

        // Find python command (pip/venv)

        #define SET_COMMAND_NAME(setting, commandName) \
            do { \
                if (!settings.setting) \
                    return bypass(L"settings." L ## #setting L" = false"); \
                command = command_name_t::commandName; \
            } while (0)

        wstring pythonPath;
        wstring scriptsDir;
        if ((wcsistarts(exeName, L"pip") || wcsistarts(exeName, L"virtualenv")) && wcsiends(exeName, L".exe")) {
            scriptsDir = PathGetDirNameW(exePath.c_str());
            auto pythonSearchPath = PathGetDirNameW(scriptsDir.c_str()) + L";" + searchPath;
            if (FAILED(SearchPathW(pythonSearchPath.c_str(), L"python.exe", nullptr, pythonPath)))
                return bypass(format(L"SearchPathW({}) failed", L"python").c_str());

            if (wcsistarts(exeName, L"pip"))
                SET_COMMAND_NAME(replacePip, pip); // ... pip.exe ...
            else if (wcsistarts(exeName, L"virtualenv"))
                SET_COMMAND_NAME(replaceVirtualEnv, virtualenv); // ... virtualenv.exe ...
            else
                return bypass(L"unknown module.exe");
        } else if (wcsistarts(exeName, L"python")) {
            pythonPath = exePath;
            auto pythonDir = PathGetDirNameW(pythonPath.c_str());
            scriptsDir = pythonDir + L"\\Scripts";

            if (args.size() > iArg + 1 && wcsequals(args[iArg], L"-m")) {
                if (wcsequals(args[iArg + 1], L"pip"))
                    SET_COMMAND_NAME(replacePip, pip); // ... python.exe -m pip ...
                else if (wcsequals(args[iArg + 1], L"venv"))
                    SET_COMMAND_NAME(replaceVenv, venv); // ... python.exe -m venv ...
                else if (wcsequals(args[iArg + 1], L"virtualenv"))
                    SET_COMMAND_NAME(replaceVirtualEnv, virtualenv); // ... python.exe -m virtualenv ...
                else
                    return bypass(L"unknown python -m module");
                iArg += 2;
            } else {
                wstring scriptPath;
                if (FAILED(ExpandEnvAndSearchPathW(scriptsDir.c_str(), args[iArg], L".exe", scriptPath)))
                    return bypass(format(L"SearchPathW({}) failed", args[iArg]).c_str());

                auto scriptName = PathFindFileNameW(scriptPath.c_str());
                if (wcsistarts(scriptName, L"pip") && wcsiends(scriptName, L".exe"))
                    SET_COMMAND_NAME(replacePip, pip); // ... python.exe scripts/pip.exe ...
                else if (wcsistarts(scriptName, L"virtualenv") && wcsiends(scriptName, L".exe"))
                    SET_COMMAND_NAME(replaceVirtualEnv, virtualenv); // ... python.exe scripts/virtualenv.exe ...
                else
                    return bypass(L"unknown python module.exe");
                iArg++;
            }
        }

        #undef SET_COMMAND_NAME

        if (settings.logLevel >= log_level_t::trace)
            Wh_Log(L"  python = %s [%d@%d]", pythonPath.c_str(), command, iArg);

        if (command == command_name_t::unknown)
            return bypass(L"unrecognized command");

        // Find uv

        wstring uvPath;
        if (FAILED(SearchPathW(scriptsDir.c_str(), L"uv.exe", nullptr, uvPath)))
            uvPath = sys.defaultUvPath;
        if (uvPath.empty())
            return bypass(L"failed to find uv");

        if (settings.logLevel >= log_level_t::trace)
            Wh_Log(L"  uv = %s", uvPath.c_str());

        // Translate arguments

        vector<wstring> outArgs {
            uvPath,
            L"--no-python-downloads",
            L"--python-preference", L"only-system",
        };

        if (command == command_name_t::pip) {
            if (FAILED(translatePipArgs(args, iArg, pythonPath, outArgs)))
                return bypass(L"uv pip command not supported");
        } else if (command == command_name_t::venv) {
            if (FAILED(translateVenvArgs(venvOptions, args, iArg, pythonPath, outArgs)))
                return bypass(L"uv venv command not supported");
        } else if (command == command_name_t::virtualenv) {
            if (FAILED(translateVenvArgs(virtualenvOptions, args, iArg, pythonPath, outArgs)))
                return bypass(L"uv virtualenv command not supported");
        } else {
            throw runtime_error("unexpected command");
        }

        if (settings.logLevel >= log_level_t::trace)
            for (size_t i = 0; i < outArgs.size(); i++)
                Wh_Log(L"  -> arg[%d] = %s", i, outArgs.at(i).c_str());

        // Call original CreateProcessW with translated arguments

        auto newApplicationName = outArgs[0];
        auto newCommandLine = ArgvToCommandLine<vector<wstring>&, wchar_t>(outArgs);
        if (settings.logLevel >= log_level_t::info)
            Wh_Log(L"-> CreateProcessW (\"%s\", \"%s\")",
                repr(newApplicationName.c_str()), repr(newCommandLine.data()));

        // return fail();
        return CreateProcessW_Original(
            newApplicationName.c_str(), newCommandLine.data(),
            lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
            lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
    });
}

// MARK: main

HRESULT FindGlobalUvPath(wstring& uvPath) {
    auto envPath = GetEnvironmentVariableW<wstring>(L"PATH");
    if (settings.uvPath.empty()) {
        RETURN_IF_FAILED(SearchPathW(envPath.c_str(), L"uv.exe", nullptr, uvPath));
    } else {
        auto expandedUvPath = ExpandEnvironmentStringsW(settings.uvPath.c_str());
        auto attr = GetFileAttributesW(expandedUvPath.get());
        RETURN_LAST_ERROR_IF_EXPECTED(attr == INVALID_FILE_ATTRIBUTES);
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
            RETURN_IF_FAILED(SearchPathW(expandedUvPath.get(), L"uv.exe", nullptr, uvPath));
        else
            RETURN_IF_FAILED(SearchPathW(envPath.c_str(), expandedUvPath.get(), L".exe", uvPath));
    }
    return S_OK;
}

void LoadSettings() {
    settings.replacePip = Wh_GetBoolSetting(L"replacePip");
    settings.replaceVenv = Wh_GetBoolSetting(L"replaceVenv");
    settings.replaceVirtualEnv = Wh_GetBoolSetting(L"replaceVirtualEnv");
    settings.uvPath = Wh_GetStdStringSetting(L"uvPath");
    settings.uvCacheDir = Wh_GetStdStringSetting(L"uvCacheDir.uvCacheDir");
    settings.respectCacheDirArg = Wh_GetBoolSetting(L"uvCacheDir.respectCacheDirArg");
    settings.uvLinkMode = Wh_GetStdStringSetting(L"uvLinkMode.uvLinkMode");
    settings.respectLinkModeArg = Wh_GetBoolSetting(L"uvLinkMode.respectLinkModeArg");
    auto logLevel = Wh_GetStdStringSetting(L"debug.logLevel");
    settings.logLevel = logLevels.contains(logLevel) ? logLevels.at(logLevel) : log_level_t::info;

    if (FAILED(FindGlobalUvPath(sys.defaultUvPath)))
        sys.defaultUvPath = L"";

    if (settings.logLevel >= log_level_t::info)
        Wh_Log(L"defaultUvPath = %s", sys.defaultUvPath.c_str());
    if (settings.logLevel >= log_level_t::debug)
        Wh_Log(L"logLevel = %s | %d", logLevel.c_str(), settings.logLevel);
}

BOOL Wh_ModInit() {
    return Attempt(L"ModInit", []() noexcept { return FALSE; }, []() {
        if (settings.logLevel >= log_level_t::info) // technically unconditional
            Wh_Log(L"ModInit in %s", GetModuleFileNameW().get());

        LoadSettings();

        Wh_SetFunctionHook((void*)CreateProcessW, (void*)CreateProcessW_Hook, (void**)&CreateProcessW_Original);

        SetResultLoggingCallback([](const FailureInfo& failure) __stdcall noexcept {
            const size_t wilLogMessageSize = 2048;
            wchar_t logMessage[wilLogMessageSize];
            if (settings.logLevel >= log_level_t::warn && SUCCEEDED(GetFailureLogString(logMessage, wilLogMessageSize, failure)))
                Wh_Log(L"[wil] %s", logMessage);
        });
        return TRUE;
    });
}

void Wh_ModUninit() {
    Attempt(L"ModUninit", DoNothing, []() {
        if (settings.logLevel >= log_level_t::info)
            Wh_Log(L"ModUninit");
    });
}

void Wh_ModSettingsChanged() {
    Attempt(L"ModSettingsChanged", DoNothing, []() {
        if (settings.logLevel >= log_level_t::info)
            Wh_Log(L"ModSettingsChanged");
        LoadSettings();
    });
}