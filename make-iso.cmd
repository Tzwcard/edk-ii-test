@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0make-iso.ps1" %*
exit /b %ERRORLEVEL%
