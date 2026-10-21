// params.h - Parametros, rangos y presets de fabrica de MilsToz Mic
#pragma once
#include <cmath>
#include <algorithm>

enum { NUM_SLOTS = 4 };

// Parametros de CADA slot (cada slot guarda EQ + perillas completas)
enum SlotParam {
    SP_HPF = 0,                    // filtro pasa-altos (Hz)  [20 = apagado]
    SP_B1_F, SP_B1_G,              // B1: low shelf     (Hz, dB)
    SP_B2_F, SP_B2_G, SP_B2_Q,     // B2: peak  "barro / caja"
    SP_B3_F, SP_B3_G, SP_B3_Q,     // B3: peak  "nasal / cuerpo"
    SP_B4_F, SP_B4_G, SP_B4_Q,     // B4: peak  "presencia / claridad"
    SP_B5_F, SP_B5_G,              // B5: high shelf    "aire"
    SP_GATE,                       // perilla noise gate   0 = APAGADO, 1 = maximo
    SP_COMP,                       // perilla compresor SOFT (al inicio)  0 = APAGADO, 1 = maximo
    SP_DEESS,                      // perilla de-esser     0 = APAGADO, 1 = maximo
    SP_OUT,                        // volumen de salida (dB)
    // ---- NUEVOS (van al final para que los ajustes guardados con la version anterior sigan valiendo)
    SP_COMP2,                      // perilla compresor HARD (al final, antes del volumen)
    SP_CRYSTAL,                    // perilla DeeCrystal todo-en-uno: 0% .. 100% = de 0 a 5 instancias apiladas
    SP_BODY,                       // perilla Cuerpo (color analogico en graves, estilo SweetDrums)
    SP_COUNT
};

// Cantidad de parametros por slot en la version anterior (chunk "VEQ1")
enum { SP_COUNT_V1 = SP_OUT + 1 };

// Indice global de parametros VST: 0 = slot activo, luego NUM_SLOTS bloques de SP_COUNT
enum { PARAM_SLOT = 0, PARAM_COUNT = 1 + NUM_SLOTS * SP_COUNT };

inline int slotIndex(int slot, int sp) { return 1 + slot * SP_COUNT + sp; }

struct ParamInfo {
    const char* shortName;   // <= 4 caracteres (el nombre VST completo es "S1 " + esto)
    float minV, maxV;
    bool  logScale;
    const char* unit;
};

static const ParamInfo kInfo[SP_COUNT] = {
    {"HPF",  20.f,   300.f, true,  "Hz"},
    {"B1 F", 40.f,   400.f, true,  "Hz"},
    {"B1 G", -12.f,  12.f,  false, "dB"},
    {"B2 F", 100.f,  1000.f,true,  "Hz"},
    {"B2 G", -12.f,  12.f,  false, "dB"},
    {"B2 Q", 0.3f,   8.f,   true,  ""},
    {"B3 F", 500.f,  4000.f,true,  "Hz"},
    {"B3 G", -12.f,  12.f,  false, "dB"},
    {"B3 Q", 0.3f,   8.f,   true,  ""},
    {"B4 F", 1500.f, 8000.f,true,  "Hz"},
    {"B4 G", -12.f,  12.f,  false, "dB"},
    {"B4 Q", 0.3f,   8.f,   true,  ""},
    {"B5 F", 3000.f, 16000.f,true, "Hz"},
    {"B5 G", -12.f,  12.f,  false, "dB"},
    {"Gate", 0.f,    1.f,   false, "%"},
    {"Comp", 0.f,    1.f,   false, "%"},
    {"DeEs", 0.f,    1.f,   false, "%"},
    {"Out",  -12.f,  12.f,  false, "dB"},
    {"Cmp2", 0.f,    1.f,   false, "%"},
    {"Crys", 0.f,    1.f,   false, "%"},
    {"Body", 0.f,    1.f,   false, "%"},
};

static const char* const kSlotNames[NUM_SLOTS] = {"GRAVE", "MEDIA", "AGUDA", "MilsToz"};

// Presets de fabrica. Orden = enum SlotParam:
// HPF | B1 F,G | B2 F,G,Q | B3 F,G,Q | B4 F,G,Q | B5 F,G | GATE COMP DEESS OUT | COMP2 CRYSTAL BODY
// (los 3 ultimos son nuevos: 0 = apagado, el sonido de los slots 1-3 queda igual que antes)
static const float kPreset[NUM_SLOTS][SP_COUNT] = {
    // 1) GRAVE:
    {  75.f, 110.f,+3.5f,  220.f,+1.5f,1.0f, 1000.f,-3.5f,1.2f, 3200.f,-1.5f,1.0f, 10000.f,+2.5f,  0.f, 0.35f, 0.20f, +4.0f,  0.f, 0.f, 0.f },
    // 2) MEDIA:
    {  90.f, 120.f, 0.0f,  300.f,-2.5f,1.0f, 1000.f,-1.0f,1.0f, 3200.f,+2.5f,1.0f, 10000.f,+2.0f,  0.f, 0.40f, 0.25f,  0.0f,  0.f, 0.f, 0.f },
    // 3) AGUDA:
    { 110.f, 160.f,+1.0f,  350.f,-2.0f,1.1f, 2500.f,-1.5f,1.4f, 5000.f,+1.5f,0.9f, 10000.f,+1.5f,  0.f, 0.40f, 0.40f,  0.0f,  0.f, 0.f, 0.f },
    // 4) MILSTOZ:  (con un toque de los 3 efectos nuevos, para probar; ponlos en 0 si prefieres el sonido anterior)
    {  80.f, 130.f,+5.5f,  250.f,-2.0f,1.0f, 1100.f,-6.5f,1.1f, 3500.f,+2.5f,0.9f,  9500.f,+4.0f,  0.f, 0.50f, 0.50f, -4.4f, 0.25f, 0.50f, 0.40f },
};

// ---- utilidades de mapeo (real <-> normalizado 0..1 para el host) ----
inline float paramToNorm(const ParamInfo& p, float v) {
    v = std::min(std::max(v, p.minV), p.maxV);
    return p.logScale ? std::log(v / p.minV) / std::log(p.maxV / p.minV)
                      : (v - p.minV) / (p.maxV - p.minV);
}
inline float paramFromNorm(const ParamInfo& p, float n) {
    n = std::min(std::max(n, 0.f), 1.f);
    return p.logScale ? p.minV * std::pow(p.maxV / p.minV, n) : p.minV + n * (p.maxV - p.minV);
}

// Rango del parametro global idx (0 = slot activo)
inline ParamInfo paramInfoFor(int idx) {
    if (idx == PARAM_SLOT) return ParamInfo{"Slot", 0.f, (float)(NUM_SLOTS - 1), false, ""};
    return kInfo[(idx - 1) % SP_COUNT];
}