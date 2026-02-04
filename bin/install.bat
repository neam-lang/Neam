@echo off
REM Neam v0.3.0 Installation Script for Windows
REM Run as Administrator for system-wide install

set INSTALL_DIR=%1
if "%INSTALL_DIR%"=="" set INSTALL_DIR=C:\Program Files\Neam

echo Installing Neam v0.3.0...
echo   Target: %INSTALL_DIR%
echo.

if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

for %%f in (neamc.exe neam.exe neam-cli.exe neam-api.exe neam-pkg.exe neam-lsp.exe neam-dap.exe neam-gym.exe libneam.dll neam_c_api.h) do (
    if exist "%%f" (
        copy /Y "%%f" "%INSTALL_DIR%\" >nul
        echo   Installed: %%f
    )
)

echo.
echo Installation complete!
echo.
echo Add %INSTALL_DIR% to your PATH:
echo   setx PATH "%%PATH%%;%INSTALL_DIR%"
echo.
echo Quick start:
echo   neamc program.neam -o program.neamb   # Compile
echo   neam program.neamb                     # Run bytecode
echo   neam-cli                               # Interactive REPL
echo.
echo For Python bindings:  pip install neam
echo For Node.js bindings: npm install @neam-lang/neam
echo Set NEAM_LIBRARY_PATH=%INSTALL_DIR%\libneam.dll for FFI bindings
