@echo off
if "%TELEGRAM_API_ID%"=="" exit /b 2
if "%TELEGRAM_API_HASH%"=="" exit /b 2
call "C:\BuildTools\VS2026\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 || exit /b 1
set "PATH=C:\Users\Asus\.cache\codex-runtimes\codex-primary-runtime\dependencies\python;C:\Users\Asus\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\git\cmd;C:\Users\Asus\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\git\usr\bin;%PATH%"
cd /d "%~dp0tdesktop-7.1.2\Telegram"
call configure.bat qt6 -G "Ninja Multi-Config" -D TDESKTOP_API_ID=%TELEGRAM_API_ID% -D TDESKTOP_API_HASH=%TELEGRAM_API_HASH% -D CMAKE_CONFIGURATION_TYPES=Debug -D CMAKE_MSVC_DEBUG_INFORMATION_FORMAT= -D DESKTOP_APP_DISABLE_AUTOUPDATE=ON -D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON
exit /b %ERRORLEVEL%
