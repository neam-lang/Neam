@echo off
REM ============================================================================
REM  Neam v0.6.2 - Windows Quick Install Script
REM ============================================================================
REM  Usage:
REM    1. Download this script
REM    2. Right-click and "Run as Administrator" (optional, for system-wide)
REM    3. Or just double-click for user installation
REM ============================================================================

setlocal EnableDelayedExpansion

set VERSION=v0.6.2
set INSTALL_DIR=%USERPROFILE%\.neam
set BIN_DIR=%INSTALL_DIR%\bin
set DOWNLOAD_URL=https://github.com/neam-lang/Neam/releases/download/%VERSION%/neam-%VERSION%-windows-x64.zip

echo.
echo ========================================
echo   Neam %VERSION% - Windows Quick Install
echo ========================================
echo.

REM Check for PowerShell (needed for download)
where powershell >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] PowerShell is required but not found.
    echo Please install PowerShell or download manually from:
    echo %DOWNLOAD_URL%
    pause
    exit /b 1
)

echo [1/5] Creating install directory...
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
echo        %INSTALL_DIR%
echo.

echo [2/5] Downloading Neam %VERSION%...
set TEMP_ZIP=%TEMP%\neam-%VERSION%.zip
powershell -Command "& {[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%DOWNLOAD_URL%' -OutFile '%TEMP_ZIP%'}" 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Download failed. Please check your internet connection.
    echo You can manually download from:
    echo %DOWNLOAD_URL%
    pause
    exit /b 1
)
echo        Download complete.
echo.

echo [3/5] Extracting files...
powershell -Command "& {Expand-Archive -Path '%TEMP_ZIP%' -DestinationPath '%INSTALL_DIR%' -Force}" 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Extraction failed.
    pause
    exit /b 1
)
del "%TEMP_ZIP%" 2>nul
echo        Extraction complete.
echo.

echo [4/5] Configuring PATH...
REM Check if already in PATH
echo %PATH% | find /I "%BIN_DIR%" >nul
if %ERRORLEVEL% EQU 0 (
    echo        PATH already configured.
) else (
    REM Add to user PATH permanently
    for /f "tokens=2*" %%a in ('reg query "HKCU\Environment" /v Path 2^>nul') do set "USER_PATH=%%b"
    if defined USER_PATH (
        setx PATH "%USER_PATH%;%BIN_DIR%" >nul 2>nul
    ) else (
        setx PATH "%BIN_DIR%" >nul 2>nul
    )
    echo        Added %BIN_DIR% to user PATH.
    echo        [NOTE] Restart your terminal for PATH changes to take effect.
)
echo.

REM Update current session PATH
set "PATH=%BIN_DIR%;%PATH%"

echo [5/5] Verifying installation...
echo.

if exist "%BIN_DIR%\neamc.exe" (
    echo   [OK] neamc.exe (compiler)
) else (
    echo   [FAIL] neamc.exe not found
)

if exist "%BIN_DIR%\neam.exe" (
    echo   [OK] neam.exe (runtime)
) else (
    echo   [FAIL] neam.exe not found
)

if exist "%BIN_DIR%\neam-cli.exe" (
    echo   [OK] neam-cli.exe (REPL)
) else (
    echo   [FAIL] neam-cli.exe not found
)

echo.
echo ========================================
echo   Installation Complete!
echo ========================================
echo.
echo IMPORTANT: Close and reopen your terminal
echo for PATH changes to take effect.
echo.
echo ========================================
echo   Quick Start
echo ========================================
echo.
echo   # Create a new project
echo   neam-pkg init my-agent
echo   cd my-agent
echo.
echo   # Or run Hello World directly
echo   echo { emit "Hello from Neam!"; } ^> hello.neam
echo   neamc hello.neam -o hello.neamb
echo   neam hello.neamb
echo.
echo   # Start interactive REPL
echo   neam-cli
echo.
echo ========================================
echo   For AI Agents
echo ========================================
echo.
echo   Option 1: Local LLM with Ollama (free)
echo     Download from: https://ollama.com
echo     Then run: ollama pull llama3.2:3b
echo.
echo   Option 2: OpenAI API
echo     set OPENAI_API_KEY=sk-your-key-here
echo.
echo Documentation: https://github.com/neam-lang/Neam
echo.
pause
