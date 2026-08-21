@echo off
REM ================================================================
REM  build.bat — Compilar simulacion del porton (C puro, Windows)
REM  
REM  Requisitos: gcc (MinGW o MSYS2) en el PATH
REM  Ejecutar desde la carpeta PORTON_C_SIM
REM ================================================================

echo Compilando PORTON C puro (simulacion Windows)...

gcc -Wall -Wextra -O2 -o porton.exe porton.c sim_windows.c

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Fallo la compilacion.
    echo.
    echo Asegurate de tener gcc (MinGW) instalado y en el PATH.
    echo Descarga MinGW: https://www.mingw-w64.org/
    pause
    exit /b 1
)

echo.
echo [OK] porton.exe compilado correctamente.
echo Ejecuta: porton.exe
echo.
pause
