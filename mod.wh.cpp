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
// @compilerOptions -lcomdlg32
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
    $description: When enabled, respects --cache-dir and --no-cache-dir arguments passed to pip
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

#include "windhawk_api.h"

#include <shellapi.h>
#include <winuser.h>
#include <wil/stl.h>
#include <wil/win32_helpers.h>

#define MAX_OPTION_STR 10

using namespace std;
using namespace wil;

struct {
    wstring uvPath;
    wstring cmdPath;
} sys;

struct {
    bool replacePip;
    bool replacePythonPip;
    bool replacePythonVenv;
    wchar_t uvPath[MAX_PATH];
    wchar_t uvCacheDir[MAX_PATH];
    bool respectCacheDirArg;
    wchar_t uvLinkMode[MAX_OPTION_STR];
    bool respectLinkModeArg;
} settings;

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

template <typename string_type, size_t length = 256>
HRESULT ExpandEnvAndSearchPath(PCWSTR input, PCWSTR extension, string_type& result) WI_NOEXCEPT
{
    unique_cotaskmem_string expandedName;
    RETURN_IF_FAILED((ExpandEnvironmentStringsW<string_type, length>(input, expandedName)));
    const HRESULT searchResult = (SearchPathW<string_type, length>(nullptr, expandedName.get(), extension, result));
    RETURN_HR_IF_EXPECTED(searchResult, searchResult == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    RETURN_IF_FAILED(searchResult);
    return S_OK;
}

BOOL WINAPI CreateProcessW_Hook(
    LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes, LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags, LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo, LPPROCESS_INFORMATION lpProcessInformation
) {
    Wh_Log(
        L"CreateProcessW called with:\n"
        L"  lpApplicationName: %s\n"
        L"  lpCommandLine: %s",
        lpApplicationName ? lpApplicationName : L"(null)",
        lpCommandLine ? lpCommandLine : L"(null)",
        lpCurrentDirectory ? lpCurrentDirectory : L"(null)"
    );

    WCHAR message[1024];
    wsprintfW(
        message,
        L"CreateProcessW called with:\n"
        L"  lpApplicationName: %s\n"
        L"  lpCommandLine: %s",
        lpApplicationName ? lpApplicationName : L"(null)",
        lpCommandLine ? lpCommandLine : L"(null)",
        lpCurrentDirectory ? lpCurrentDirectory : L"(null)"
    );

    MessageBoxW(NULL, message, L"CreateProcessW Hook", MB_OK);

    try {
        // TODO
        // unique_hlocal_string str;
        // unique_hlocal_array_ptr<unique_hlocal_string> strs;
        // auto s = make_unique_string<unique_hlocal_string>(L"");
        // auto str = ExpandEnvironmentStringsW(L"");
    }
    catch (const exception& ex) {
        Wh_Log((L"CreateProcessW failed: " + string_to_wstring(ex.what())).c_str());
        return FALSE;
    }

    // Always use https://github.com/microsoft/wil/wiki/Win32-helpers if possible

    // TODO
    // * Parse command line - use CommandLineToArgvW (then LocalFree)
    // * Skip "cmd.exe /c" (/c is case-insensitive) and "python.exe"
    // * Accept executables in any form (full path, relative path, env variables references, .exe missing, any case etc.) - try resolving executable like os
    //    * ExpandEnvironmentStringsW > SearchPathW
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

    return CreateProcessW_Original(
        lpApplicationName, lpCommandLine,
        lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
        lpEnvironment, lpCurrentDirectory,
        lpStartupInfo, lpProcessInformation);
}

void LoadSettings() {
    settings.replacePip = Wh_GetIntSetting(L"replacePip");
    settings.replacePythonPip = Wh_GetIntSetting(L"replacePythonPip");
    settings.replacePythonVenv = Wh_GetIntSetting(L"replacePythonVenv");
    Wh_GetStringValue(L"uvPath", settings.uvPath, MAX_PATH);
    Wh_GetStringValue(L"uvCacheDir", settings.uvCacheDir, MAX_PATH);
    settings.respectCacheDirArg = Wh_GetIntSetting(L"respectCacheDirArg");
    Wh_GetStringValue(L"uvLinkMode", settings.uvLinkMode, MAX_OPTION_STR);
    settings.respectLinkModeArg = Wh_GetIntSetting(L"respectLinkModeArg");

    if (wcslen(settings.uvPath) == 0)
        THROW_IF_FAILED(SearchPathW(nullptr, L"uv.exe", nullptr, sys.uvPath));
    else {
        auto expandedUvPath = ExpandEnvironmentStringsW(settings.uvPath);
        auto attr = GetFileAttributes(expandedUvPath.get());
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
            THROW_IF_FAILED(SearchPathW(expandedUvPath.get(), L"uv.exe", nullptr, sys.uvPath));
        else
            THROW_IF_FAILED(SearchPathW(nullptr, expandedUvPath.get(), L".exe", sys.uvPath));
    }

    sys.cmdPath = GetEnvironmentVariableW<wstring>(L"ComSpec");
}

BOOL Wh_ModInit() {
    Wh_Log(L"Init");

    try {
        LoadSettings();
        Wh_SetFunctionHook((void*)CreateProcessW, (void*)CreateProcessW_Hook, (void**)&CreateProcessW_Original);
        return TRUE;
    }
    catch (const exception& ex) {
        Wh_Log((L"Initialization failed: " + string_to_wstring(ex.what())).c_str());
        return FALSE;
    }
}

void Wh_ModUninit() {
    Wh_Log(L"Uninit");
}

void Wh_ModSettingsChanged() {
    Wh_Log(L"SettingsChanged");
    LoadSettings();
}