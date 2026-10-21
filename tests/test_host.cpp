// test_host.cpp - host VST2 minimo para verificar plugin.cpp (ABI, parametros, chunk, audio)
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include "../src/vst2_abi.h"
#include "../src/params.h"

extern "C" AEffect* VSTPluginMain(audioMasterCallback);

static int fails = 0, automateCalls = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("  FALLA: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float) {
    if (op == audioMasterVersion) return 2400;
    if (op == audioMasterAutomate) ++automateCalls;
    return 0;
}

int main() {
    AEffect* e = VSTPluginMain(host);
    CHECK(e != nullptr, "VSTPluginMain devolvio null");
    if (!e) return 1;
    CHECK(e->magic == kEffectMagic, "magic incorrecto");
    CHECK(e->numInputs == 2 && e->numOutputs == 2, "E/S");
    CHECK(e->initialDelay == 0, "latencia debe ser 0");
    CHECK((e->flags & effFlagsCanReplacing) && (e->flags & effFlagsHasEditor) && (e->flags & effFlagsProgramChunks), "flags");
    CHECK(e->numParams == PARAM_COUNT, "numParams");
    std::printf("sizeof(AEffect)=%zu, numParams=%d, flags=%d\n", sizeof(AEffect), e->numParams, e->flags);

    e->dispatcher(e, effOpen, 0, 0, nullptr, 0);
    e->dispatcher(e, effSetSampleRate, 0, 0, nullptr, 48000.f);
    e->dispatcher(e, effSetBlockSize, 0, 480, nullptr, 0);
    e->dispatcher(e, effMainsChanged, 0, 1, nullptr, 0);

    // nombres: maximo 7 caracteres + terminador (buffers VST de 8 bytes)
    for (int i = 0; i < e->numParams; ++i) {
        char nm[64], dp[64], lb[64];
        std::memset(nm, 'X', 64); std::memset(dp, 'X', 64); std::memset(lb, 'X', 64);
        e->dispatcher(e, effGetParamName, i, 0, nm, 0);
        e->dispatcher(e, effGetParamDisplay, i, 0, dp, 0);
        e->dispatcher(e, effGetParamLabel, i, 0, lb, 0);
        CHECK(std::strlen(nm) <= 7, "nombre largo idx %d: %s", i, nm);
        CHECK(std::strlen(dp) <= 7, "display largo idx %d: %s", i, dp);
        CHECK(std::strlen(lb) <= 7, "label largo idx %d: %s", i, lb);
        CHECK(nm[8] == 'X' && dp[8] == 'X' && lb[8] == 'X', "escritura fuera de 8 bytes idx %d", i);
    }
    char nm[64];
    e->dispatcher(e, effGetParamName, slotIndex(0, SP_GATE), 0, nm, 0);
    std::printf("param %d = '%s'\n", slotIndex(0, SP_GATE), nm);
    char dp[64];
    e->dispatcher(e, effGetParamDisplay, slotIndex(0, SP_GATE), 0, dp, 0);
    CHECK(std::strcmp(dp, "OFF") == 0, "gate debe estar OFF por defecto (es '%s')", dp);

    // set/get con normalizados
    int gi = slotIndex(1, SP_B3_F);
    e->setParameter(e, gi, 0.5f);
    float back = e->getParameter(e, gi);
    CHECK(std::fabs(back - 0.5f) < 1e-4f, "roundtrip parametro: %f", back);
    CHECK(automateCalls == 0, "setParameter del host no debe re-notificar");

    // chunk roundtrip
    e->setParameter(e, slotIndex(2, SP_COMP), 0.77f);
    void* data = nullptr;
    VstIntPtr bytes = e->dispatcher(e, effGetChunk, 0, 0, &data, 0);
    CHECK(bytes == (VstIntPtr)(8 + 4 * PARAM_COUNT) && data, "tamano chunk %zd", (ssize_t)bytes);
    std::vector<unsigned char> saved((unsigned char*)data, (unsigned char*)data + bytes);
    e->setParameter(e, slotIndex(2, SP_COMP), 0.1f);
    e->dispatcher(e, effSetChunk, 0, bytes, saved.data(), 0);
    CHECK(std::fabs(e->getParameter(e, slotIndex(2, SP_COMP)) - 0.77f) < 1e-4f, "chunk no restauro el valor");
    unsigned char garbage[16] = {1, 2, 3};
    e->dispatcher(e, effSetChunk, 0, 16, garbage, 0);            // basura: no debe romper nada
    CHECK(std::fabs(e->getParameter(e, slotIndex(2, SP_COMP)) - 0.77f) < 1e-4f, "chunk basura altero el estado");

    // proceso estereo, in-place, en bloques de 480
    std::vector<float> L(480), R(480);
    double pk = 0; bool finite = true;
    for (int b = 0; b < 200; ++b) {
        for (int i = 0; i < 480; ++i) { L[i] = R[i] = 0.2f * (float)std::sin(2 * 3.14159265 * 440.0 * (b * 480 + i) / 48000.0); }
        float* io[2] = {L.data(), R.data()};
        e->processReplacing(e, io, io, 480);
        for (int i = 0; i < 480; ++i) { pk = std::fmax(pk, std::fabs(L[i])); if (!std::isfinite(L[i])) finite = false; }
    }
    CHECK(finite && pk > 0.05 && pk < 0.9, "audio de salida raro: pico=%f", pk);
    std::printf("proceso OK, pico de salida = %.3f\n", pk);

    // cambiar de slot en vivo
    e->setParameter(e, 0, 1.0f / 3.0f * 3.0f);   // slot 4 (celular)
    float* io[2] = {L.data(), R.data()};
    e->processReplacing(e, io, io, 480);
    char sd[64]; e->dispatcher(e, effGetParamDisplay, 0, 0, sd, 0);
    CHECK(std::strcmp(sd, "4") == 0, "slot activo debe mostrar 4, muestra %s", sd);

    // ajustes guardados con la version anterior (chunk "VEQ1", 18 parametros por slot) deben seguir valiendo
    {
        const int oldPer = SP_COUNT_V1, oldCount = 1 + NUM_SLOTS * oldPer;
        std::vector<unsigned char> old(8 + 4 * oldCount, 0);
        const unsigned hdr[2] = {0x31514556u, (unsigned)oldCount};
        std::memcpy(old.data(), hdr, 8);
        auto put = [&](int idx, float f) { std::memcpy(old.data() + 8 + 4 * idx, &f, 4); };
        for (int i = 0; i < oldCount; ++i) put(i, 0.f);
        put(0, 2.f);                                        // slot activo = 3
        put(1 + 2 * oldPer + SP_COMP, 0.65f);               // slot 3: comp soft
        put(1 + 3 * oldPer + SP_OUT, -3.f);                 // slot 4: volumen
        put(1 + 3 * oldPer + SP_B3_F, 1234.f);              // slot 4: frecuencia B3
        e->dispatcher(e, effSetChunk, 0, (VstIntPtr)old.size(), old.data(), 0);
        CHECK(std::fabs(e->getParameter(e, slotIndex(2, SP_COMP)) - 0.65f) < 1e-4f, "migracion: comp del slot 3");
        float outN = e->getParameter(e, slotIndex(3, SP_OUT));
        CHECK(std::fabs(paramFromNorm(kInfo[SP_OUT], outN) - (-3.f)) < 1e-3f, "migracion: volumen del slot 4");
        float b3 = e->getParameter(e, slotIndex(3, SP_B3_F));
        CHECK(std::fabs(paramFromNorm(kInfo[SP_B3_F], b3) - 1234.f) < 0.5f, "migracion: B3 F del slot 4");
        char sd2[64]; e->dispatcher(e, effGetParamDisplay, 0, 0, sd2, 0);
        CHECK(std::strcmp(sd2, "3") == 0, "migracion: slot activo debe ser 3 (es '%s')", sd2);
        // los efectos nuevos conservan su valor de fabrica
        CHECK(std::fabs(paramFromNorm(kInfo[SP_CRYSTAL], e->getParameter(e, slotIndex(3, SP_CRYSTAL))) - kPreset[3][SP_CRYSTAL]) < 1e-3f,
              "migracion: DeeCrystal debe quedar en su valor de fabrica");
        char dn[64]; e->dispatcher(e, effGetParamDisplay, slotIndex(0, SP_CRYSTAL), 0, dn, 0);
        CHECK(std::strcmp(dn, "OFF") == 0, "DeeCrystal del slot 1 debe mostrar OFF (es '%s')", dn);
    }

    ERect* r = nullptr;
    CHECK(e->dispatcher(e, effEditGetRect, 0, 0, &r, 0) == 1 && r && r->right == 700 && r->bottom == 430, "rect del editor");

    char nn[64]; e->dispatcher(e, effGetEffectName, 0, 0, nn, 0);
    CHECK(std::strcmp(nn, "MilsToz Mic") == 0, "nombre");
    CHECK(e->dispatcher(e, effGetVstVersion, 0, 0, nullptr, 0) == 2400, "version VST");

    e->dispatcher(e, effClose, 0, 0, nullptr, 0);
    std::printf("\n%s (%d fallas)\n", fails ? "RESULTADO: HAY FALLAS" : "RESULTADO: TODO OK", fails);
    return fails ? 1 : 0;
}
