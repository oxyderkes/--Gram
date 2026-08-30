@echo off
set "VSLANG=1033"
call "C:\BuildTools\VS2026\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 || exit /b 1
set "PATH=C:\Users\Asus\.cache\codex-runtimes\codex-primary-runtime\dependencies\python;C:\Users\Asus\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\git\cmd;C:\Users\Asus\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\git\usr\bin;%PATH%"
set "CLEAN_ARG="
if /i "%~1"=="--clean-first" set "CLEAN_ARG=--clean-first"
cd /d "%~dp0tdesktop-7.1.2"
cmake --build out --config Debug --target Telegram %CLEAN_ARG% --parallel
exit /b %ERRORLEVEL%
