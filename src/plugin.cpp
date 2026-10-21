// plugin.cpp - MilsToz Mic: plugin VST 2.x (64 bits) para Equalizer APO
#include <cstring>
#include <cstdio>
#include <vector>
#include <new>
#include "vst2_abi.h"
#include "state.h"
#include "dsp.h"
#include "gui.h"

#if defined(__SSE2__) || defined(_M_X64)
  #include <xmmintrin.h>
  #define VEQ_HAVE_SSE 1
#endif

#if defined(_WIN32)
  #define VST_EXPORT extern "C" __declspec(dllexport)
#else
  #define VST_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

constexpr uint32_t kChunkMagicV1 = 0x31514556;   // "VEQ1": version anterior (18 parametros por slot)
constexpr uint32_t kChunkMagic   = 0x32514556;   // "VEQ2": version actual

struct Plugin {
    AEffect effect;
    audioMasterCallback master;
    SharedState st;
    dsp::VoiceEngine engine;
    uint32_t seenVersion = 0;
    ERect rect{0, 0, 0, 0};
    void* gui = nullptr;
    std::vector<uint8_t> chunk;
    std::vector<float> tmp[2];

    explicit Plugin(audioMasterCallback m) : master(m) {}
};

Plugin* P(AEffect* e) { return static_cast<Plugin*>(e->object); }

void copyStr(void* dst, const char* s, size_t maxLen) {
    if (!dst) return;
    std::strncpy(static_cast<char*>(dst), s, maxLen - 1);
    static_cast<char*>(dst)[maxLen - 1] = 0;
}

void hostNotify(void* user, int idx, float norm) {
    Plugin* p = static_cast<Plugin*>(user);
    if (p->master) p->master(&p->effect, audioMasterAutomate, idx, 0, nullptr, norm);
}

void paramName(int idx, char* out8) {
    if (idx == PARAM_SLOT) { std::snprintf(out8, 8, "Slot"); return; }
    const int s = (idx - 1) / SP_COUNT, sp = (idx - 1) % SP_COUNT;
    std::snprintf(out8, 8, "S%d %s", s + 1, kInfo[sp].shortName);   // maximo 7 caracteres
}

void paramDisplay(int idx, float real, char* out8) {
    if (idx == PARAM_SLOT) { std::snprintf(out8, 8, "%d", (int)(real + 0.5f) + 1); return; }
    const int sp = (idx - 1) % SP_COUNT;
    switch (sp) {
        case SP_GATE: case SP_COMP: case SP_DEESS: case SP_COMP2: case SP_CRYSTAL: case SP_BODY:
            if (real < 0.005f) std::snprintf(out8, 8, "OFF"); else std::snprintf(out8, 8, "%d", (int)(real * 100.f + 0.5f));
            break;
        case SP_HPF: case SP_B1_F: case SP_B2_F: case SP_B3_F: case SP_B4_F: case SP_B5_F:
            std::snprintf(out8, 8, "%d", (int)(real + 0.5f)); break;
        case SP_B2_Q: case SP_B3_Q: case SP_B4_Q:
            std::snprintf(out8, 8, "%.2f", real); break;
        default:
            std::snprintf(out8, 8, "%+.1f", real); break;
    }
}

const char* paramLabel(int idx) {
    if (idx == PARAM_SLOT) return "";
    return kInfo[(idx - 1) % SP_COUNT].unit;
}

// ------------------------------------------------------------ audio
void processReplacing(AEffect* e, float** in, float** out, VstInt32 n) {
    Plugin* p = P(e);
    if (n <= 0) return;

    const uint32_t v = p->st.version.load(std::memory_order_acquire);
    if (v != p->seenVersion) {
        p->seenVersion = v;
        float sp[SP_COUNT];
        const int s = p->st.slot();
        for (int i = 0; i < SP_COUNT; ++i) sp[i] = p->st.getSlotParam(s, i);
        p->engine.configure(sp);
    }
#ifdef VEQ_HAVE_SSE
    const unsigned csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040);          // FTZ + DAZ: evita numeros denormales (picos de CPU)
#endif
    p->engine.process(in, out, n);
#ifdef VEQ_HAVE_SSE
    _mm_setcsr(csr);
#endif
}

void processAccumulating(AEffect* e, float** in, float** out, VstInt32 n) {   // API vieja, por compatibilidad
    Plugin* p = P(e);
    if (n <= 0) return;
    p->tmp[0].resize(n); p->tmp[1].resize(n);
    float* o[2] = {p->tmp[0].data(), p->tmp[1].data()};
    processReplacing(e, in, o, n);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) out[c][i] += o[c][i];
}

void processDouble(AEffect*, double**, double**, VstInt32) {}   // no se anuncia soporte

// ------------------------------------------------------------ parametros
void setParameter(AEffect* e, VstInt32 idx, float value) { P(e)->st.setNormalized(idx, value); }
float getParameter(AEffect* e, VstInt32 idx) {
    if (idx < 0 || idx >= PARAM_COUNT) return 0.f;
    return P(e)->st.normalized(idx);
}

// ------------------------------------------------------------ dispatcher
VstIntPtr dispatcher(AEffect* e, VstInt32 op, VstInt32 index, VstIntPtr value, void* ptr, float opt) {
    Plugin* p = P(e);
    switch (op) {
        case effOpen: return 0;
        case effClose:
            if (p->gui) { guiClose(p->gui); p->gui = nullptr; }
            delete p;
            return 0;
        case effSetProgram: case effSetProgramName: return 0;
        case effGetProgram: return 0;
        case effGetProgramName: copyStr(ptr, "Default", 24); return 0;

        case effGetParamLabel:
            if (index < 0 || index >= PARAM_COUNT) return 0;
            copyStr(ptr, paramLabel(index), 8); return 0;
        case effGetParamDisplay:
            if (index < 0 || index >= PARAM_COUNT || !ptr) return 0;
            paramDisplay(index, p->st.get(index), static_cast<char*>(ptr)); return 0;
        case effGetParamName:
            if (index < 0 || index >= PARAM_COUNT || !ptr) return 0;
            paramName(index, static_cast<char*>(ptr)); return 0;

        case effSetSampleRate:
            if (opt > 1000.f) p->engine.setSampleRate((double)opt);
            p->seenVersion = 0;      // fuerza recalculo de coeficientes
            return 0;
        case effSetBlockSize: return 0;
        case effMainsChanged: if (value) p->engine.reset(); return 0;

        case effEditGetRect:
            if (!ptr) return 0;
            p->rect.top = 0; p->rect.left = 0; p->rect.bottom = (int16_t)GUI_H; p->rect.right = (int16_t)GUI_W;   // (ver gui.h)
            *static_cast<ERect**>(ptr) = &p->rect;
            return 1;
        case effEditOpen:
            if (p->gui) { guiClose(p->gui); p->gui = nullptr; }
            p->gui = guiOpen(&p->st, ptr);
            return p->gui ? 1 : 0;
        case effEditClose:
            if (p->gui) { guiClose(p->gui); p->gui = nullptr; }
            return 0;
        case effEditIdle: return 0;

        case effGetChunk: {       // guarda TODOS los slots (es lo que Equalizer APO guarda en config.txt)
            const size_t bytes = 8 + sizeof(float) * PARAM_COUNT;
            p->chunk.resize(bytes);
            const uint32_t hdr[2] = {kChunkMagic, (uint32_t)PARAM_COUNT};
            std::memcpy(p->chunk.data(), hdr, 8);
            for (int i = 0; i < PARAM_COUNT; ++i) {
                const float f = p->st.get(i);
                std::memcpy(p->chunk.data() + 8 + 4 * i, &f, 4);
            }
            if (ptr) *static_cast<void**>(ptr) = p->chunk.data();
            return (VstIntPtr)bytes;
        }
        case effSetChunk: {
            if (!ptr || value < 8) return 0;
            const uint8_t* d = static_cast<const uint8_t*>(ptr);
            uint32_t hdr[2]; std::memcpy(hdr, d, 8);
            if (hdr[0] != kChunkMagic && hdr[0] != kChunkMagicV1) return 0;
            const int count = (int)std::min<uint32_t>(hdr[1], (uint32_t)((value - 8) / 4));
            if (hdr[0] == kChunkMagicV1) {
                // Ajustes guardados con la version anterior: cada slot tenia menos parametros.
                // Se reubican; los efectos nuevos conservan su valor de fabrica.
                if (count >= 1) { float f; std::memcpy(&f, d + 8, 4); p->st.setReal(PARAM_SLOT, f, false); }
                for (int s = 0; s < NUM_SLOTS; ++s)
                    for (int q = 0; q < SP_COUNT_V1; ++q) {
                        const int src = 1 + s * SP_COUNT_V1 + q;
                        if (src >= count) continue;
                        float f; std::memcpy(&f, d + 8 + 4 * src, 4);
                        p->st.setReal(slotIndex(s, q), f, false);
                    }
                return 0;
            }
            for (int i = 0; i < count && i < PARAM_COUNT; ++i) {
                float f; std::memcpy(&f, d + 8 + 4 * i, 4);
                p->st.setReal(i, f, false);
            }
            return 0;
        }

        case effCanBeAutomated: return (index >= 0 && index < PARAM_COUNT) ? 1 : 0;
        case effGetPlugCategory: return kPlugCategEffect;
        case effGetEffectName: copyStr(ptr, "MilsToz Mic", 32); return 1;
        case effGetVendorString: copyStr(ptr, "MilsToz", 64); return 1;
        case effGetProductString: copyStr(ptr, "MilsToz Mic", 64); return 1;
        case effGetVendorVersion: return 1000;
        case effCanDo: return 0;
        case effGetVstVersion: return 2400;
        default: return 0;
    }
}

} // namespace

VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback master) {
    if (!master) return nullptr;
    master(nullptr, audioMasterVersion, 0, 0, nullptr, 0.f);

    Plugin* p = new (std::nothrow) Plugin(master);
    if (!p) return nullptr;

    std::memset(&p->effect, 0, sizeof(AEffect));
    AEffect& e = p->effect;
    e.magic = kEffectMagic;
    e.dispatcher = dispatcher;
    e.process = processAccumulating;
    e.setParameter = setParameter;
    e.getParameter = getParameter;
    e.numPrograms = 1;
    e.numParams = PARAM_COUNT;
    e.numInputs = 2;
    e.numOutputs = 2;
    e.flags = effFlagsHasEditor | effFlagsCanReplacing | effFlagsProgramChunks;
    e.initialDelay = 0;                       // latencia cero
    e.object = p;
    e.uniqueID = ('M' << 24) | ('T' << 16) | ('Z' << 8) | 'm';
    e.version = 1000;
    e.processReplacing = processReplacing;
    e.processDoubleReplacing = processDouble;

    p->st.notify = hostNotify;
    p->st.notifyUser = p;
    return &e;
}
