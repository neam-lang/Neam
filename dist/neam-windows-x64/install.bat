@echo off
echo Installing Neam v1.4.0 to %LOCALAPPDATA%\Neam...
set INSTALL_DIR=%LOCALAPPDATA%\Neam
mkdir "%INSTALL_DIR%" 2>/dev/null
for %%f in (neamc.exe neam.exe neam-api.exe neam-lambda.exe neam-lsp.exe neam-pkg.exe neam-gym.exe) do (
    if exist "%%f" (
        copy /Y "%%f" "%INSTALL_DIR%\" >/dev/null
        echo   Installed: %%f
    )
)
echo.
echo Adding %INSTALL_DIR% to PATH...
setx PATH "%INSTALL_DIR%;%PATH%" >/dev/null 2>&1
echo.
echo Installation complete! Open a new terminal and run: neamc --help
