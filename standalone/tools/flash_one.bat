@echo off
setlocal
set PYTHONUTF8=1
cd /d "%~dp0"
if exist "%USERPROFILE%\.espressif\python_env\idf6.0_py3.11_env\Scripts\python.exe" (
  "%USERPROFILE%\.espressif\python_env\idf6.0_py3.11_env\Scripts\python.exe" flash_one.py
) else (
  python flash_one.py
)
echo.
pause
