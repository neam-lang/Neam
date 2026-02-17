@echo off
REM Neam v0.7.2 Windows Installation Script
REM Run as Administrator for system-wide install

SET INSTALL_DIR=%1
IF "%INSTALL_DIR%"=="" SET INSTALL_DIR=C:\neam\bin

echo Installing Neam v0.7.2 to %INSTALL_DIR%...

IF NOT EXIST "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

FOR %%F IN (neamc.exe neam.exe neam-api.exe neam-pkg.exe neam-lambda.exe) DO (
    IF EXIST "%%F" (
        copy /Y "%%F" "%INSTALL_DIR%\" >nul
        echo   Installed: %%F
    )
)

IF EXIST "stdlib" (
    IF NOT EXIST "%INSTALL_DIR%\..\share\neam\stdlib" mkdir "%INSTALL_DIR%\..\share\neam\stdlib"
    xcopy /E /Y /Q "stdlib\*" "%INSTALL_DIR%\..\share\neam\stdlib\" >nul
    echo   Installed: stdlib
)

echo.
echo Installation complete!
echo.
echo Add %INSTALL_DIR% to your PATH:
echo   setx PATH "%%PATH%%;%INSTALL_DIR%"
echo.
echo Quick start:
echo   neamc hello.neam -o hello.neamb
echo   neam hello.neamb
echo.
pause
