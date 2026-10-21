@echo off
REM ============================================================
REM  Compila VoiceEQMic.dll (VST2, 64 bits) con Visual Studio
REM  Abre "x64 Native Tools Command Prompt for VS" y ejecuta:  build_msvc.bat
REM ============================================================
if not exist build mkdir build
cl /nologo /O2 /GL /MT /EHsc /std:c++17 /LD /utf-8 /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX ^
   src\plugin.cpp src\gui_win32.cpp ^
   /Fo:build\ /Fe:build\VoiceEQMic.dll ^
   /link /LTCG user32.lib gdi32.lib
if errorlevel 1 (
  echo.
  echo *** ERROR de compilacion. Copia el mensaje de arriba y pegalo en el chat. ***
  exit /b 1
)
echo.
echo Listo: build\VoiceEQMic.dll
