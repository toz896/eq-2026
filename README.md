# MilsToz Mic  (VST 2.x, 64 bits, para Equalizer APO)

EQ de 5 bandas + HPF con 4 slots de voz, y una cadena de "microfono profesional" con una perilla por efecto.
Latencia 0 muestras, sin memoria dinamica en el audio, cada etapa en 0 = salta (CPU cero).

## Cadena de audio (en este orden)
`HPF -> Noise Gate -> Comp SOFT -> EQ 5 bandas -> Cuerpo -> DeeCrystal -> De-esser -> Comp HARD -> Volumen -> Limitador`

| Perilla | Que hace |
|---|---|
| NOISE GATE | Puerta/expansor suave (equivale a ATKExpander + ReaGate). |
| COMP SOFT | Compresor suave al inicio: ataque/suelta lentos, razon 1.5:1 a 4:1. Parejo y transparente. |
| CUERPO | Color analogico en graves (estilo SweetDrums): satura suave por debajo de ~200 Hz y agrega peso. |
| DEECRYSTAL | Todo-en-uno: 0% a 100% = de 0 a 5 instancias de excitador de armonicos encadenadas. 20% = 1, 40% = 2, ... 100% = las 5. |
| DE-ESSER | Baja la sibilancia (sss) que pueden agregar el EQ y DeeCrystal. |
| COMP HARD | Compresor duro al final, justo antes del volumen: ataque/suelta rapidos, razon 3:1 a 10:1. Firme y al frente. |
| VOLUMEN | Ganancia de salida (el limitador a -1 dBFS protege contra picos). |

Consumo medido (todo activado, DeeCrystal al 100%): ~1.3% de UN nucleo; memoria fija de unos pocos KB.

## Compilar (elige UNA opcion)
1. GitHub Actions (sin instalar nada): sube esta carpeta a un repositorio -> pestana Actions -> descarga `MilsTozMic-Plugin`.
2. Visual Studio Build Tools: abre "x64 Native Tools Command Prompt" y ejecuta `build_msvc.bat`.
3. MinGW-w64 (64 bits): `build_mingw.bat`.

Resultado: `build\VoiceEQMic.dll`  ->  copiala a `C:\Program Files\EqualizerAPO\VSTPlugins\`

Los ajustes guardados con la version anterior se migran solos (los efectos nuevos quedan en su valor de fabrica).

## Archivos
- `src/dsp.h`        motor de audio (EQ, gate, 2 compresores, cuerpo, DeeCrystal, de-esser, limitador)
- `src/params.h`     parametros, rangos y los 4 presets (aqui cambias las voces)
- `src/state.h`      estado compartido sin bloqueos
- `src/plugin.cpp`   plugin VST2 (entrada, dispatcher, chunks, proceso)
- `src/gui_win32.cpp` interfaz (GDI puro)
- `src/vst2_abi.h`   definiciones minimas de la ABI VST2
- `tests/`           pruebas (`tests/run_tests.sh` en Linux/WSL)
