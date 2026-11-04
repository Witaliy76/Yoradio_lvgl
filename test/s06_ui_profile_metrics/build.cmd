@echo off
rem S06 host proof: BoardDefaults selection, resolved metrics and viewport compatibility.
call "C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I ..\..\.pio\build\4848S040\config /Fe:ui_profile_metrics.exe ui_profile_metrics.cpp
