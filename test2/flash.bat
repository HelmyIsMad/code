@echo off
setlocal enabledelayedexpansion

:: --- CONFIGURATION ---
set BUILD_DIR=build/debug
set PROJECT_NAME=test2
set FLASH_ADDRESS=0x08000000

:: 1. BUILD THE PROJECT
echo [*] Starting Build...
cmake --build %BUILD_DIR% --parallel 8

if %ERRORLEVEL% NEQ 0 (
    echo [!] Build Failed!
    exit /b %ERRORLEVEL%
)
echo [*] Build Successful.

:: 2. FLASH THE PROJECT
echo [*] Flashing %PROJECT_NAME%.elf to MCU...
STM32_Programmer_CLI.exe -c port=SWD mode=UR -d "%BUILD_DIR%/%PROJECT_NAME%.elf" %FLASH_ADDRESS% -rst

if %ERRORLEVEL% NEQ 0 (
    echo [!] Flashing Failed! Check connection.
    exit /b %ERRORLEVEL%
)

echo [*] Done! Application is running.