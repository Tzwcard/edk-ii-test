@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-iso.ps1" %*
exit /b %ERRORLEVEL%
