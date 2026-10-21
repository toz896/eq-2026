// vst2_abi.h
// Definiciones minimas de la ABI de VST 2.x escritas a mano (layout binario de AEffect,
// opcodes y ERect). NO incluye codigo del SDK de Steinberg.
// Solo contiene lo que este plugin necesita.
//
// "VST" es marca registrada de Steinberg Media Technologies GmbH.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef int32_t  VstInt32;
typedef intptr_t VstIntPtr;

struct AEffect;

typedef VstIntPtr (*audioMasterCallback)(AEffect* effect, VstInt32 opcode, VstInt32 index,
                                         VstIntPtr value, void* ptr, float opt);
typedef VstIntPtr (*AEffectDispatcherProc)(AEffect* effect, VstInt32 opcode, VstInt32 index,
                                           VstIntPtr value, void* ptr, float opt);
typedef void (*AEffectProcessProc)(AEffect* effect, float** inputs, float** outputs, VstInt32 sampleFrames);
typedef void (*AEffectProcessDoubleProc)(AEffect* effect, double** inputs, double** outputs, VstInt32 sampleFrames);
typedef void (*AEffectSetParameterProc)(AEffect* effect, VstInt32 index, float parameter);
typedef float (*AEffectGetParameterProc)(AEffect* effect, VstInt32 index);

struct AEffect {
    VstInt32 magic;                       // 'VstP'
    AEffectDispatcherProc dispatcher;
    AEffectProcessProc process;           // obsoleto (acumulativo)
    AEffectSetParameterProc setParameter;
    AEffectGetParameterProc getParameter;
    VstInt32 numPrograms;
    VstInt32 numParams;
    VstInt32 numInputs;
    VstInt32 numOutputs;
    VstInt32 flags;
    VstIntPtr resvd1;
    VstIntPtr resvd2;
    VstInt32 initialDelay;                // latencia en muestras
    VstInt32 realQualities;
    VstInt32 offQualities;
    float    ioRatio;
    void*    object;
    void*    user;
    VstInt32 uniqueID;
    VstInt32 version;
    AEffectProcessProc processReplacing;
    AEffectProcessDoubleProc processDoubleReplacing;
    char     future[56];
};

struct ERect {
    int16_t top, left, bottom, right;
};

enum {
    kEffectMagic = 0x56737450  // 'VstP'
};

enum VstAEffectFlags {
    effFlagsHasEditor     = 1 << 0,
    effFlagsCanReplacing  = 1 << 4,
    effFlagsProgramChunks = 1 << 5
};

enum AEffectOpcodes {
    effOpen = 0,
    effClose = 1,
    effSetProgram = 2,
    effGetProgram = 3,
    effSetProgramName = 4,
    effGetProgramName = 5,
    effGetParamLabel = 6,
    effGetParamDisplay = 7,
    effGetParamName = 8,
    effSetSampleRate = 10,
    effSetBlockSize = 11,
    effMainsChanged = 12,
    effEditGetRect = 13,
    effEditOpen = 14,
    effEditClose = 15,
    effEditIdle = 19,
    effGetChunk = 23,
    effSetChunk = 24,
    effProcessEvents = 25,
    effCanBeAutomated = 26,
    effGetPlugCategory = 35,
    effGetEffectName = 45,
    effGetVendorString = 47,
    effGetProductString = 48,
    effGetVendorVersion = 49,
    effCanDo = 51,
    effGetVstVersion = 58
};

enum AudioMasterOpcodes {
    audioMasterAutomate = 0,
    audioMasterVersion = 1
};

enum { kPlugCategEffect = 1 };
