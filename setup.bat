@echo off
REM ============================================
REM  Neam v0.6.2 - Windows Quick Setup Script
REM ============================================
REM  This script sets up Neam for the current
REM  terminal session and verifies installation.
REM ============================================

echo.
echo ========================================
echo   Neam v0.6.2 - Windows Quick Setup
echo ========================================
echo.

REM Add bin directory to PATH for this session
set "NEAM_HOME=%~dp0bin"
set "PATH=%NEAM_HOME%;%PATH%"

echo [1/4] Setting PATH to include Neam binaries...
echo        NEAM_HOME = %NEAM_HOME%
echo.

echo [2/4] Verifying Neam installation...
echo.

echo   Checking neamc (compiler)...
neamc --version 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] neamc not found or not working
) else (
    echo   [OK] neamc found
)

echo   Checking neam (runtime)...
neam --version 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] neam not found or not working
) else (
    echo   [OK] neam found
)

echo   Checking neam-cli (REPL)...
where neam-cli >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] neam-cli not found
) else (
    echo   [OK] neam-cli found
)

echo.
echo [3/4] Running Hello World test...
echo.

echo { emit "Hello from Neam on Windows!"; } > %TEMP%\hello.neam
neamc %TEMP%\hello.neam -o %TEMP%\hello.neamb 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] Compilation failed
    goto :end
)
echo   [OK] Compilation successful

neam %TEMP%\hello.neamb
if %ERRORLEVEL% NEQ 0 (
    echo   [FAIL] Execution failed
) else (
    echo   [OK] Execution successful
)

del %TEMP%\hello.neam %TEMP%\hello.neamb 2>nul

echo.
echo [4/4] Setup complete!
echo.
echo ========================================
echo   Next Steps:
echo ========================================
echo.
echo   1. Run examples:
echo      neamc examples\01_hello_world.neam -o hello.neamb
echo      neam hello.neamb
echo.
echo   2. Start interactive REPL:
echo      neam-cli
echo.
echo   3. For AI agent examples (requires Ollama):
echo      - Install Ollama: https://ollama.com
echo      - Run: ollama pull llama3.2:3b
echo      - Then: neamc examples\06_agent_ollama.neam -o agent.neamb
echo              neam agent.neamb
echo.
echo   4. For OpenAI examples:
echo      set OPENAI_API_KEY=sk-your-key-here
echo      neamc examples\07_agent_openai.neam -o agent.neamb
echo      neam agent.neamb
echo.

:end
