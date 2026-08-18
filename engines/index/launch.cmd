@echo off
setlocal
cd /d "%~dp0"
if exist "runtime\.venv\Scripts\python.exe" (
    "runtime\.venv\Scripts\python.exe" worker.py
) else (
    py -3.11 worker.py
)
