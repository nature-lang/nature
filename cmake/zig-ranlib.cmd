@echo off
zig ranlib %*
exit /b %ERRORLEVEL%
