@echo off
REM ============================================================
REM  Compila VoiceEQMic.dll (VST2, 64 bits) con MinGW-w64 (g++ de 64 bits, p. ej. MSYS2 UCRT64)
REM ============================================================
if not exist build mkdir build
g++ -std=c++17 -O2 -shared -static -static-libgcc -static-libstdc++ -msse2 -DNDEBUG -DNOMINMAX -s ^
    -o build\VoiceEQMic.dll src\plugin.cpp src\gui_win32.cpp -luser32 -lgdi32
if errorlevel 1 (
  echo.
  echo *** ERROR de compilacion. Copia el mensaje de arriba y pegalo en el chat. ***
  exit /b 1
)
echo.
echo Listo: build\VoiceEQMic.dll
