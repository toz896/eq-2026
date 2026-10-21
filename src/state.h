// state.h - Estado compartido (hilo de audio <-> host <-> GUI). Sin bloqueos (atomics).
#pragma once
#include <atomic>
#include <cstdint>
#include <cmath>
#include "params.h"

struct SharedState {
    std::atomic<float>    v[PARAM_COUNT];     // valores REALES (Hz, dB, 0..1)
    std::atomic<uint32_t> version{1};         // sube con cada cambio
    void (*notify)(void* user, int index, float normalized) = nullptr;
    void* notifyUser = nullptr;

    SharedState() { resetToFactory(); }

    void resetToFactory() {
        v[PARAM_SLOT].store(1.f);             // slot inicial: MEDIA
        for (int s = 0; s < NUM_SLOTS; ++s)
            for (int p = 0; p < SP_COUNT; ++p)
                v[slotIndex(s, p)].store(kPreset[s][p]);
        version.fetch_add(1);
    }

    float get(int idx) const { return v[idx].load(std::memory_order_relaxed); }

    int slot() const {
        int s = (int)(get(PARAM_SLOT) + 0.5f);
        return s < 0 ? 0 : (s >= NUM_SLOTS ? NUM_SLOTS - 1 : s);
    }
    float getSlotParam(int s, int sp) const { return get(slotIndex(s, sp)); }

    float normalized(int idx) const {
        ParamInfo pi = paramInfoFor(idx);
        return paramToNorm(pi, get(idx));
    }

    // Cambia un valor real (con limites). notifyHost=false cuando el cambio viene del host.
    void setReal(int idx, float real, bool notifyHost = true) {
        if (idx < 0 || idx >= PARAM_COUNT || !std::isfinite(real)) return;
        ParamInfo pi = paramInfoFor(idx);
        real = std::min(std::max(real, pi.minV), pi.maxV);
        if (idx == PARAM_SLOT) real = std::floor(real + 0.5f);
        v[idx].store(real, std::memory_order_relaxed);
        version.fetch_add(1, std::memory_order_release);
        if (notifyHost && notify) notify(notifyUser, idx, paramToNorm(pi, real));
    }

    void setNormalized(int idx, float n) {
        if (idx < 0 || idx >= PARAM_COUNT || !std::isfinite(n)) return;
        ParamInfo pi = paramInfoFor(idx);
        setReal(idx, paramFromNorm(pi, n), false);
    }
};
