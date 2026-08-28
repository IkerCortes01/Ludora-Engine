@echo off
REM ============================================================
REM  Ludora Engine - script de compilacion
REM    build.bat            -> compila Release
REM    build.bat debug      -> compila Debug (sale a bin\debug\)
REM    build.bat run        -> compila Release y ejecuta
REM    build.bat test       -> compila y lanza el autodiagnostico
REM    build.bat clean      -> borra build/ y bin/
REM ============================================================
setlocal

REM Situarse en la carpeta del script pase lo que pase: si el shell que
REM invoca esto esta dentro de bin\, borrar esa carpeta dejaria un
REM directorio de trabajo invalido y las rutas relativas fallarian.
cd /d "%~dp0"

set ROOT=%~dp0
set CONFIG=Release
set ACTION=%1
if /i "%ACTION%"=="debug" set CONFIG=Debug

if /i "%ACTION%"=="clean" (
    echo Limpiando...
    if exist "%ROOT%build" rmdir /s /q "%ROOT%build"
    if exist "%ROOT%bin"   rmdir /s /q "%ROOT%bin"
    echo Listo.
    exit /b 0
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake no esta en el PATH.
    echo Instalalo desde https://cmake.org/download/
    exit /b 1
)

if not exist "%ROOT%build" (
    echo === Configurando proyecto ===
    cmake -S "%ROOT%." -B "%ROOT%build" -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 (
        echo [ERROR] Fallo la configuracion de CMake.
        exit /b 1
    )
)

echo === Compilando %CONFIG% ===
cmake --build "%ROOT%build" --config %CONFIG%
if errorlevel 1 (
    echo [ERROR] Fallo la compilacion.
    exit /b 1
)

REM Debug sale a bin\debug\ para no pisar el binario de Release.
set OUTDIR=%ROOT%bin
if /i "%CONFIG%"=="Debug" set OUTDIR=%ROOT%bin\debug

echo.
echo === Compilado: %OUTDIR%\Ludora.exe ===

if /i "%ACTION%"=="run"  start "" "%OUTDIR%\Ludora.exe"
if /i "%ACTION%"=="test" (
    pushd "%OUTDIR%"
    if exist selftest-report.txt del selftest-report.txt
    REM .\ explicito: el directorio actual no esta en PATH
    .\Ludora.exe --selftest
    echo.
    if exist selftest-report.txt (type selftest-report.txt) else (echo [ERROR] No se genero el informe.)
    popd
)

endlocal
exit /b 0
