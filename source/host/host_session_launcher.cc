//
// PROJECT:         Aspia
// FILE:            host/host_session_launcher.cc
// LICENSE:         GNU Lesser General Public License 2.1
// PROGRAMMERS:     Dmitry Chapyshev (dmitry@aspia.ru)
//

#include "host/host_session_launcher.h"

#include <QDebug>

#include <userenv.h>
#include <wtsapi32.h>
#include <string>

#include "base/win/scoped_object.h"
#include "base/command_line.h"
#include "base/scoped_native_library.h"
#include "base/system_error_code.h"
#include "host/host_switches.h"

namespace aspia {

namespace {

constexpr wchar_t kProcessNameHost[] = L"aspia_host.exe";

// Name of the default session desktop.
constexpr wchar_t kDefaultDesktopName[] = L"winsta0\\default";

bool GetCurrentFolder(std::wstring* path)
{
    wchar_t buffer[MAX_PATH] = { 0 };

    if (!GetModuleFileNameW(nullptr, buffer, _countof(buffer)))
    {
        qWarning() << "GetModuleFileNameW failed: " << lastSystemErrorString();
        return false;
    }

    std::wstring_view temp(buffer);

    size_t pos = temp.find_last_of(L"\\/");
    if (pos == std::wstring_view::npos)
        return false;

    path->assign(temp.substr(0, pos));
    return true;
}

bool CopyProcessToken(DWORD desired_access, ScopedHandle* token_out)
{
    ScopedHandle process_token;

    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_DUPLICATE | desired_access,
                          process_token.Recieve()))
    {
        qWarning() << "OpenProcessToken failed: " << lastSystemErrorString();
        return false;
    }

    if (!DuplicateTokenEx(process_token,
                          desired_access,
                          nullptr,
                          SecurityImpersonation,
                          TokenPrimary,
                          token_out->Recieve()))
    {
        qWarning() << "DuplicateTokenEx failed: " << lastSystemErrorString();
        return false;
    }

    return true;
}

// Creates a copy of the current process with SE_TCB_NAME privilege enabled.
bool CreatePrivilegedToken(ScopedHandle* token_out)
{
    ScopedHandle privileged_token;
    const DWORD desired_access = TOKEN_ADJUST_PRIVILEGES | TOKEN_IMPERSONATE |
        TOKEN_DUPLICATE | TOKEN_QUERY;

    if (!CopyProcessToken(desired_access, &privileged_token))
        return false;

    // Get the LUID for the SE_TCB_NAME privilege.
    TOKEN_PRIVILEGES state;
    state.PrivilegeCount = 1;
    state.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!LookupPrivilegeValueW(nullptr, SE_TCB_NAME, &state.Privileges[0].Luid))
    {
        qWarning() << "LookupPrivilegeValueW failed: " << lastSystemErrorString();
        return false;
    }

    // Enable the SE_TCB_NAME privilege.
    if (!AdjustTokenPrivileges(privileged_token, FALSE, &state, 0,
                               nullptr, nullptr))
    {
        qWarning() << "AdjustTokenPrivileges failed: " << lastSystemErrorString();
        return false;
    }

    token_out->Reset(privileged_token.Release());
    return true;
}

// Creates a copy of the current process token for the given |session_id| so
// it can be used to launch a process in that session.
bool CreateSessionToken(DWORD session_id, ScopedHandle* token_out)
{
    ScopedHandle session_token;
    const DWORD desired_access = TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID |
        TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY;

    if (!CopyProcessToken(desired_access, &session_token))
        return false;

    ScopedHandle privileged_token;

    if (!CreatePrivilegedToken(&privileged_token))
        return false;

    if (!ImpersonateLoggedOnUser(privileged_token))
    {
        qWarning() << "ImpersonateLoggedOnUser failed: " << lastSystemErrorString();
        return false;
    }

    // Change the session ID of the token.
    BOOL ret = SetTokenInformation(session_token, TokenSessionId, &session_id, sizeof(session_id));

    BOOL reverted = RevertToSelf();
    if (!reverted)
        qFatal("RevertToSelf failed");

    if (!ret)
    {
        qWarning() << "SetTokenInformation failed: " << lastSystemErrorString();
        return false;
    }

    DWORD ui_access = 1;
    if (!SetTokenInformation(session_token, TokenUIAccess, &ui_access, sizeof(ui_access)))
    {
        qWarning() << "SetTokenInformation failed: " << lastSystemErrorString();
        return false;
    }

    token_out->Reset(session_token.Release());
    return true;
}

bool CreateProcessWithToken(HANDLE user_token, const CommandLine& command_line)
{
    STARTUPINFOW startup_info;
    memset(&startup_info, 0, sizeof(startup_info));

    startup_info.cb = sizeof(startup_info);
    startup_info.lpDesktop = const_cast<wchar_t*>(kDefaultDesktopName);

    PVOID environment = nullptr;

    if (!CreateEnvironmentBlock(&environment, user_token, FALSE))
    {
        qWarning() << "CreateEnvironmentBlock failed: " << lastSystemErrorString();
        return false;
    }

    PROCESS_INFORMATION process_info;
    memset(&process_info, 0, sizeof(process_info));

    if (!CreateProcessAsUserW(user_token,
                              nullptr,
                              const_cast<LPWSTR>(command_line.GetCommandLineString().c_str()),
                              nullptr,
                              nullptr,
                              FALSE,
                              CREATE_UNICODE_ENVIRONMENT | HIGH_PRIORITY_CLASS,
                              environment,
                              nullptr,
                              &startup_info,
                              &process_info))
    {
        qWarning() << "CreateProcessAsUserW failed: " << lastSystemErrorString();
        DestroyEnvironmentBlock(environment);
        return false;
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    DestroyEnvironmentBlock(environment);

    return true;
}

bool LaunchSessionProcessAsUser(const std::wstring& session_type,
                                uint32_t session_id,
                                const std::wstring& channel_id)
{
    ScopedHandle privileged_token;

    if (!CreatePrivilegedToken(&privileged_token))
        return false;

    ScopedHandle session_token;

    if (!ImpersonateLoggedOnUser(privileged_token))
    {
        qWarning() << "ImpersonateLoggedOnUser failed: " << lastSystemErrorString();
        return false;
    }

    BOOL ret = WTSQueryUserToken(session_id, session_token.Recieve());

    BOOL reverted = RevertToSelf();
    if (!reverted)
        qFatal("RevertToSelft failed");

    if (!ret)
    {
        qWarning() << "WTSQueryUserToken failed: " << lastSystemErrorString();
        return false;
    }

    std::wstring program_path;

    if (!GetCurrentFolder(&program_path))
        return false;

    program_path.append(L"\\");
    program_path.append(kProcessNameHost);

    CommandLine command_line(program_path);

    command_line.AppendSwitch(kSessionTypeSwitch, session_type);
    command_line.AppendSwitch(kChannelIdSwitch, channel_id);

    return CreateProcessWithToken(session_token, command_line);
}

bool LaunchSessionProcessAsSystem(const std::wstring& session_type,
                                  uint32_t session_id,
                                  const std::wstring& channel_id)
{
    std::wstring program_path;

    if (!GetCurrentFolder(&program_path))
        return false;

    program_path.append(L"\\");
    program_path.append(kProcessNameHost);

    CommandLine command_line(program_path);

    command_line.AppendSwitch(kSessionTypeSwitch, session_type);
    command_line.AppendSwitch(kChannelIdSwitch, channel_id);

    ScopedHandle session_token;

    if (!CreateSessionToken(session_id, &session_token))
        return false;

    return CreateProcessWithToken(session_token, command_line);
}

} // namespace

bool LaunchSessionProcess(proto::auth::SessionType session_type,
                          uint32_t session_id,
                          const std::wstring& channel_id)
{
    switch (session_type)
    {
        case proto::auth::SESSION_TYPE_DESKTOP_MANAGE:
        case proto::auth::SESSION_TYPE_DESKTOP_VIEW:
        {
            return LaunchSessionProcessAsSystem(kSessionTypeDesktop, session_id, channel_id);
        }

        case proto::auth::SESSION_TYPE_FILE_TRANSFER:
        {
            return LaunchSessionProcessAsUser(kSessionTypeFileTransfer, session_id, channel_id);
        }

        default:
        {
            qWarning() << "Unknown session type: " << session_type;
            return false;
        }
    }
}

} // namespace aspia
