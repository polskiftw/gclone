@echo off
setlocal
set "ENGINE_ROOT=%LOCALAPPDATA%\gclone\runtimes\index"
set "GCLONE_INDEX_ROOT=%ENGINE_ROOT%"
set "HF_HOME=%ENGINE_ROOT%\hf"
if not exist "%ENGINE_ROOT%\.venv\Scripts\python.exe" exit /b 86
if not exist "%ENGINE_ROOT%\source\indextts\infer_v2_5.py" exit /b 87
cd /d "%ENGINE_ROOT%"
"%ENGINE_ROOT%\.venv\Scripts\python.exe" "%~dp0worker.py"
