@echo off
setlocal
set "RUNTIME=%LOCALAPPDATA%\gclone\runtimes\qwen"
set "HF_HOME=%RUNTIME%\hf"
if not exist "%RUNTIME%\.venv\Scripts\python.exe" exit /b 86
cd /d "%RUNTIME%"
"%RUNTIME%\.venv\Scripts\python.exe" "%~dp0worker.py"
