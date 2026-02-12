@echo off
REM Neam v0.7.1 Installation Script for Windows

set INSTALL_DIR=%1
if "%INSTALL_DIR%"=="" set INSTALL_DIR=C:\Program Files\Neam

echo Installing Neam v0.7.1 to %INSTALL_DIR%...

if not exist "%INSTALL_DIR%" (
    mkdir "%INSTALL_DIR%"
)

for %%f in (neamc.exe neam.exe neam-api.exe neam-lsp.exe neam-pkg.exe neam-gym.exe) do (
    if exist "%%f" (
        copy /Y "%%f" "%INSTALL_DIR%\"
        echo   Installed: %%f
    )
)

echo.
echo Installation complete!
echo.
echo Add %INSTALL_DIR% to your PATH:
echo   setx PATH "%%PATH%%;%INSTALL_DIR%"
echo.
echo Run 'neamc --help' to compile programs.
