@echo off
rem headless --snapshot build of the sim with MSVC (no SDL, no LVGL libs, no make).
rem   build.bat [W H]        default 448 368; e.g. build.bat 480 480 for the 2.16" square panel trial
rem writes out\fishsnap_WxH.exe, runs it, and leaves PNGs in out\WxH\ (see README.md)
setlocal
cd /d "%~dp0"
set W=%1
set H=%2
if "%W%"=="" set W=448
if "%H%"=="" set H=368
set VCV=
for %%V in (18 2022 2019) do for %%E in (BuildTools Community Professional Enterprise) do (
  if not defined VCV if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCV=%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
  if not defined VCV if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCV=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCV ( echo no Visual Studio C++ build tools found & exit /b 1 )
call "%VCV%" >nul 2>nul
if not exist out\obj mkdir out\obj
set P=..\..
cl /nologo /O2 /std:c17 /utf-8 /W1 /D_CRT_SECURE_NO_WARNINGS /D_USE_MATH_DEFINES /DTANK_W=%W% /DTANK_H=%H% /DLV_CONF_INCLUDE_SIMPLE /DSOUNDS_BIN="\"../assets/sounds/sounds.bin\"" /DPT_VERSION="\"sim%W%\"" ^
 /FI compat.h /I stub /I "%P%\sim" /I "%P%\common" /I "%P%\common\llm" /Fo"out\obj\\" /Fe"out\fishsnap_%W%x%H%.exe" ^
 "%P%\sim\main.c" stubs.c "%P%\sim\persist_port_sim.c" "%P%\sim\net_port_sim.c" "%P%\common\update.c" "%P%\common\tank.c" "%P%\common\advisor.c" "%P%\common\render.c" "%P%\common\icons.c" "%P%\common\progression.c" "%P%\common\setup.c" "%P%\common\audio.c" "%P%\common\notice.c" "%P%\common\sounds.c" "%P%\common\llm\q4_model.c" "%P%\common\llm\word_tok.c" "%P%\common\llm\advisor_core.c" | findstr /r /c:"error" /c:"LNK"
if not exist "out\fishsnap_%W%x%H%.exe" ( echo build failed & exit /b 1 )
if not exist "out\%W%x%H%\ppm" mkdir "out\%W%x%H%\ppm"
"out\fishsnap_%W%x%H%.exe" --snapshot "out/%W%x%H%/ppm/s" 20 2>nul | findstr snapshot
python topng.py "out/%W%x%H%/ppm" "out/%W%x%H%"
