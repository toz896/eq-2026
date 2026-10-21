// dsp.h - Motor de audio de MilsToz Mic.
// Cadena (todo por muestra, SIN lookahead => latencia = 0 muestras):
//   HPF -> Noise Gate -> Comp SOFT -> EQ 5 bandas -> Cuerpo -> DeeCrystal -> De-esser
//       -> Comp HARD -> Volumen -> Limitador
// Cada etapa se salta por completo si su perilla esta en 0 (cero CPU).
// Sin memoria dinamica en el hilo de audio: todo son variables fijas dentro de la clase.
#pragma once
#include <cmath>
#include <complex>
#include <algorithm>
#include "params.h"

namespace dsp {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- Biquad (RBJ)
struct BiquadCoef {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

inline BiquadCoef makeCoef(double b0, double b1, double b2, double a0, double a1, double a2) {
    BiquadCoef c; const double inv = 1.0 / a0;
    c.b0 = b0 * inv; c.b1 = b1 * inv; c.b2 = b2 * inv; c.a1 = a1 * inv; c.a2 = a2 * inv;
    return c;
}
inline double clampFreq(double fs, double f) { return std::min(std::max(f, 10.0), fs * 0.45); }

inline BiquadCoef highpass(double fs, double f, double q) {
    f = clampFreq(fs, f);
    const double w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
    return makeCoef((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}
inline BiquadCoef lowpass(double fs, double f, double q) {
    f = clampFreq(fs, f);
    const double w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w), al = s / (2 * q);
    return makeCoef((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}
inline BiquadCoef peaking(double fs, double f, double dB, double q) {
    f = clampFreq(fs, f);
    const double A = std::pow(10.0, dB / 40), w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
    const double al = s / (2 * q);
    return makeCoef(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
}
inline BiquadCoef lowShelf(double fs, double f, double dB) {   // pendiente S = 1
    f = clampFreq(fs, f);
    const double A = std::pow(10.0, dB / 40), w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
    const double t = 2 * std::sqrt(A) * (s / std::sqrt(2.0));
    return makeCoef(A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c),
                    A * ((A + 1) - (A - 1) * c - t),
                    (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
}
inline BiquadCoef highShelf(double fs, double f, double dB) {  // pendiente S = 1
    f = clampFreq(fs, f);
    const double A = std::pow(10.0, dB / 40), w = 2 * kPi * f / fs, c = std::cos(w), s = std::sin(w);
    const double t = 2 * std::sqrt(A) * (s / std::sqrt(2.0));
    return makeCoef(A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c),
                    A * ((A + 1) + (A - 1) * c - t),
                    (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
}

// Respuesta en frecuencia (dB) - la usa la GUI para dibujar la curva
inline double magnitudeDb(const BiquadCoef& c, double fs, double f) {
    const double w = 2 * kPi * f / fs;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
    const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const std::complex<double> den = 1.0 + c.a1 * z1 + c.a2 * z2;
    return 20.0 * std::log10(std::abs(num / den) + 1e-20);
}

// Forma transpuesta II: pocas operaciones y buen comportamiento numerico
struct BiquadState {
    double z1 = 0, z2 = 0;
    inline double process(const BiquadCoef& c, double x) {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

inline double dbToLin(double db) { return std::exp(db * 0.11512925464970229); }
inline double tcCoef(double fs, double seconds) { return std::exp(-1.0 / (fs * seconds)); }

// ---------------------------------------------------------------- Compresor (se usa 2 veces: SOFT y HARD)
// Detector de picos estereo (enlazado), rodilla suave, maquillaje automatico. Sin lookahead.
struct Compressor {
    bool on = false;
    double thr = -12, ratio = 2, knee = 8, makeup = 0;     // dB, dB
    double gr = 0, env = 0;                                // estado
    double envRel = 0, atk = 0, rel = 0;                   // coeficientes

    void setTimes(double fs, double envRelSec, double atkSec, double relSec) {
        envRel = tcCoef(fs, envRelSec); atk = tcCoef(fs, atkSec); rel = tcCoef(fs, relSec);
    }
    void resetState() { gr = 0; env = 0; }

    inline void process(double& l, double& r) {
        const double det = std::max(std::fabs(l), std::fabs(r));
        env = det > env ? det : env * envRel;
        const double xdb = 20.0 * std::log10(env + 1e-9);
        const double over = xdb - thr;
        double ydb;
        if (2.0 * over < -knee) ydb = xdb;
        else if (2.0 * std::fabs(over) <= knee) {
            const double t = over + knee * 0.5;
            ydb = xdb + (1.0 / ratio - 1.0) * t * t / (2.0 * knee);
        } else ydb = thr + over / ratio;
        const double grT = ydb - xdb;                      // <= 0
        const double c = grT < gr ? atk : rel;
        gr = grT + (gr - grT) * c;
        const double g = dbToLin(gr + makeup);
        l *= g; r *= g;
    }
};

// ---------------------------------------------------------------- Saturador suave asimetrico
// Rapido (una division). Asimetrico => genera armonicos pares e impares (sonido "analogico").
// Normalizado: para senales chicas la ganancia es 1 (solo agrega armonicos cuando sube el nivel).
struct Shaper {
    static constexpr double kBias = 0.25;
    double drive = 1.0, norm = 1.0, off = 0.0;
    void set(double d) {
        drive = d;
        const double s = 1.0 / ((1.0 + kBias) * (1.0 + kBias));   // pendiente de x/(1+|x|) en el sesgo
        norm = 1.0 / (d * s);
        off = kBias / (1.0 + kBias);
    }
    inline double process(double x) const {
        const double v = x * drive + kBias;
        return (v / (1.0 + std::fabs(v)) - off) * norm;
    }
};

// ---------------------------------------------------------------- Motor
class VoiceEngine {
public:
    VoiceEngine() { setSampleRate(48000.0); }

    void setSampleRate(double fs) {
        fs_ = std::min(std::max(fs, 8000.0), 384000.0);
        gateEnvAtk_ = tcCoef(fs_, 0.002);    // detector RMS: sube rapido...
        gateEnvRel_ = tcCoef(fs_, 0.030);    // ...y baja en 30 ms
        gateAtk_ = tcCoef(fs_, 0.002);       // abre en ~2 ms (no se come el inicio de las palabras)
        gateRel_ = tcCoef(fs_, 0.120);       // cierra suave
        gateHoldMax_ = (int)(0.080 * fs_);   // 80 ms de "hold"
        soft_.setTimes(fs_, 0.005, 0.006, 0.140);    // SOFT: ataque lento, suelta lenta (transparente)
        hard_.setTimes(fs_, 0.002, 0.0015, 0.090);   // HARD: ataque rapido, suelta rapida (firme)
        dsEnvRel_ = tcCoef(fs_, 0.020);
        dsAtk_ = tcCoef(fs_, 0.001);
        dsRel_ = tcCoef(fs_, 0.030);
        limRel_ = tcCoef(fs_, 0.060);
        if (configured_) { float p[SP_COUNT]; std::copy(cur_, cur_ + SP_COUNT, p); configure(p); }
        else reset();
    }

    // Recalcula coeficientes con los parametros de un slot (se llama solo cuando algo cambia)
    void configure(const float* p) {
        std::copy(p, p + SP_COUNT, cur_);
        configured_ = true;

        hpfOn_ = p[SP_HPF] > 21.0f;
        if (hpfOn_) hpf_ = highpass(fs_, p[SP_HPF], 0.7071);

        bool on[5];
        BiquadCoef cf[5];
        on[0] = std::fabs(p[SP_B1_G]) > 0.05f; if (on[0]) cf[0] = lowShelf(fs_, p[SP_B1_F], p[SP_B1_G]);
        on[1] = std::fabs(p[SP_B2_G]) > 0.05f; if (on[1]) cf[1] = peaking(fs_, p[SP_B2_F], p[SP_B2_G], p[SP_B2_Q]);
        on[2] = std::fabs(p[SP_B3_G]) > 0.05f; if (on[2]) cf[2] = peaking(fs_, p[SP_B3_F], p[SP_B3_G], p[SP_B3_Q]);
        on[3] = std::fabs(p[SP_B4_G]) > 0.05f; if (on[3]) cf[3] = peaking(fs_, p[SP_B4_F], p[SP_B4_G], p[SP_B4_Q]);
        on[4] = std::fabs(p[SP_B5_G]) > 0.05f; if (on[4]) cf[4] = highShelf(fs_, p[SP_B5_F], p[SP_B5_G]);
        for (int b = 0; b < 5; ++b) {
            if (on[b]) { band_[b] = cf[b]; if (!bandOn_[b]) { bandSt_[0][b].reset(); bandSt_[1][b].reset(); } }
            bandOn_[b] = on[b];
        }

        // ---- Noise gate (expansor descendente): perilla 0 = apagado
        const double ga = p[SP_GATE];
        const bool gOn = ga > 0.005;
        if (gOn) {
            const double thr = -72.0 + 42.0 * ga;            // -72 dBFS ... -30 dBFS
            const double o = dbToLin(thr), c = dbToLin(thr - 4.0);   // histeresis de 4 dB
            gateOpenThr_ = o * o;                              // se compara contra energia (RMS al cuadrado)
            gateCloseThr_ = c * c;
            gateFloor_ = dbToLin(-(18.0 + 24.0 * ga));        // reduccion -18 ... -42 dB (suave, sin cortes secos)
        }
        if (gOn && !gateOn_) { gateGain_ = 1.0; gateOpen_ = true; gateHold_ = gateHoldMax_; gateEnv_ = 0; }
        gateOn_ = gOn;

        // ---- Compresor SOFT (al inicio): baja el umbral, sube la razon y el maquillaje auto
        const double ca = p[SP_COMP];
        const bool cOn = ca > 0.005;
        if (cOn) {
            soft_.thr = -6.0 - 24.0 * ca;                     // -6 ... -30 dB
            soft_.ratio = 1.5 + 2.5 * ca;                     // 1.5:1 ... 4:1
            soft_.knee = 8.0;
            soft_.makeup = 0.5 * (-soft_.thr) * (1.0 - 1.0 / soft_.ratio);
        }
        if (cOn && !soft_.on) soft_.resetState();
        soft_.on = cOn;

        // ---- Compresor HARD (al final, antes del volumen): mas razon, ataque rapido, maquillaje contenido
        const double ch = p[SP_COMP2];
        const bool hOn = ch > 0.005;
        if (hOn) {
            hard_.thr = -8.0 - 22.0 * ch;                     // -8 ... -30 dB
            hard_.ratio = 3.0 + 7.0 * ch;                     // 3:1 ... 10:1
            hard_.knee = 4.0;
            hard_.makeup = 0.3 * (-hard_.thr) * (1.0 - 1.0 / hard_.ratio);
        }
        if (hOn && !hard_.on) hard_.resetState();
        hard_.on = hOn;

        // ---- Cuerpo (color analogico en graves, estilo SweetDrums): satura suave solo por debajo de ~200 Hz
        const double ba = p[SP_BODY];
        const bool bOn = ba > 0.005;
        if (bOn) {
            bodyLp_ = lowpass(fs_, 200.0, 0.7071);
            bodyHp_ = highpass(fs_, 50.0, 0.7071);            // quita continua / subsonico generado
            bodyShaper_.set(1.0 + 14.0 * ba);   // exagerado: satura mucho
            bodyMix_ = 1.6 * ba;                          // exagerado: hasta ~+8 dB de peso en graves
        }
        if (bOn && !bodyOn_) for (int c = 0; c < 2; ++c) { bodyLpSt_[c].reset(); bodyHpSt_[c].reset(); }
        bodyOn_ = bOn;

        // ---- DeeCrystal todo-en-uno: 0% .. 100% = de 0 a 5 instancias ENCADENADAS (acumulativas).
        // Cada instancia es un excitador de armonicos: pasa-altos -> saturador -> solo los armonicos
        // generados (se resta la parte lineal, asi no hay cancelaciones de fase) -> pasa-altos -> mezcla.
        // Perilla en 40% = 2 instancias completas; en 50% = 2 completas + la 3ra a medias. Las instancias
        // que no se usan se saltan (CPU cero), asi que el costo crece solo si subes la perilla.
        const double ya = std::min(std::max((double)p[SP_CRYSTAL], 0.0), 1.0);
        const double layers = 5.0 * ya;                       // 0 .. 5
        int nSt = (int)std::ceil(layers - 0.025);             // etapas activas (la perilla < 0.5% = apagado)
        nSt = std::min(std::max(nSt, 0), kCrystalStages);
        if (nSt > 0) {
            crysHp1_ = highpass(fs_, 3500.0, 0.7071);
            crysHp2_ = highpass(fs_, 4500.0, 0.7071);
            crysShaper_.set(kCrystalDrive);
        }
        for (int s = 0; s < kCrystalStages; ++s) {
            crysAmt_[s] = std::min(std::max(layers - s, 0.0), 1.0) * kCrystalMix;
            if (s >= crysN_ && s < nSt) for (int c = 0; c < 2; ++c) { crysSt_[s][c][0].reset(); crysSt_[s][c][1].reset(); }
        }
        crysN_ = nSt;

        // ---- De-esser (reduce solo las altas cuando hay "sss")
        const double da = p[SP_DEESS];
        const bool dOn = da > 0.005;
        if (dOn) {
            dsHp_ = highpass(fs_, 6000.0, 0.7071);            // sidechain: solo detecta "sss"
            dsLp_ = lowpass(fs_, 6000.0, 0.7071);             // separacion: agudos = x - LP(x)
            dsThrDb_ = -10.0 - 30.0 * da;
        }
        if (dOn && !dsOn_) {
            dsGr_ = 0; dsEnv_ = 0;
            for (int c = 0; c < 2; ++c) { dsSt_[c].reset(); dsLpSt_[c].reset(); }
        }
        dsOn_ = dOn;

        outGain_ = dbToLin(p[SP_OUT]);
    }

    void reset() {
        for (int c = 0; c < 2; ++c) {
            hpfSt_[c].reset(); dsSt_[c].reset(); dsLpSt_[c].reset();
            bodyLpSt_[c].reset(); bodyHpSt_[c].reset();
            for (int s = 0; s < kCrystalStages; ++s) { crysSt_[s][c][0].reset(); crysSt_[s][c][1].reset(); }
            for (int b = 0; b < 5; ++b) bandSt_[c][b].reset();
        }
        gateGain_ = 1.0; gateOpen_ = true; gateHold_ = 0; gateEnv_ = 0;
        soft_.resetState(); hard_.resetState(); dsGr_ = 0; dsEnv_ = 0; limGain_ = 1.0;
    }

    // Procesa 2 canales (in-place permitido: se lee antes de escribir)
    void process(const float* const* in, float** out, int n) {
        const float* inL = in[0]; const float* inR = in[1];
        float* outL = out[0]; float* outR = out[1];
        bool bad = false;
        for (int i = 0; i < n; ++i) {
            double l = inL[i], r = inR[i];

            if (hpfOn_) { l = hpfSt_[0].process(hpf_, l); r = hpfSt_[1].process(hpf_, r); }

            if (gateOn_) {
                const double e2 = std::max(l * l, r * r);
                gateEnv_ = e2 + (gateEnv_ - e2) * (e2 > gateEnv_ ? gateEnvAtk_ : gateEnvRel_);
                if (gateOpen_) {
                    if (gateEnv_ < gateCloseThr_) { if (gateHold_ > 0) --gateHold_; else gateOpen_ = false; }
                    else gateHold_ = gateHoldMax_;
                } else if (gateEnv_ > gateOpenThr_) { gateOpen_ = true; gateHold_ = gateHoldMax_; }
                const double target = gateOpen_ ? 1.0 : gateFloor_;
                const double c = target > gateGain_ ? gateAtk_ : gateRel_;
                gateGain_ = target + (gateGain_ - target) * c;
                l *= gateGain_; r *= gateGain_;
            }

            if (soft_.on) soft_.process(l, r);

            for (int b = 0; b < 5; ++b)
                if (bandOn_[b]) { l = bandSt_[0][b].process(band_[b], l); r = bandSt_[1][b].process(band_[b], r); }

            if (bodyOn_) {
                const double bl = bodyHpSt_[0].process(bodyHp_, bodyShaper_.process(bodyLpSt_[0].process(bodyLp_, l)));
                const double br = bodyHpSt_[1].process(bodyHp_, bodyShaper_.process(bodyLpSt_[1].process(bodyLp_, r)));
                l += bl * bodyMix_; r += br * bodyMix_;
            }

            for (int s = 0; s < crysN_; ++s) {       // DeeCrystal: instancias en cadena
                const double bl = crysSt_[s][0][0].process(crysHp1_, l), br = crysSt_[s][1][0].process(crysHp1_, r);
                const double hl = crysSt_[s][0][1].process(crysHp2_, crysShaper_.process(bl) - bl);
                const double hr = crysSt_[s][1][1].process(crysHp2_, crysShaper_.process(br) - br);
                l += hl * crysAmt_[s]; r += hr * crysAmt_[s];
            }

            if (dsOn_) {
                const double hl = dsSt_[0].process(dsHp_, l), hr = dsSt_[1].process(dsHp_, r);
                const double lvl = std::max(std::fabs(hl), std::fabs(hr));
                dsEnv_ = lvl > dsEnv_ ? lvl : dsEnv_ * dsEnvRel_;
                double gdb = 0.0;
                if (dsEnv_ > 1e-6) {
                    const double ed = 20.0 * std::log10(dsEnv_);
                    if (ed > dsThrDb_) { gdb = (dsThrDb_ - ed) * 0.75; if (gdb < -12.0) gdb = -12.0; }
                }
                const double c = gdb < dsGr_ ? dsAtk_ : dsRel_;
                dsGr_ = gdb + (dsGr_ - gdb) * c;
                const double lowL = dsLpSt_[0].process(dsLp_, l), lowR = dsLpSt_[1].process(dsLp_, r);
                if (dsGr_ < -0.01) {
                    const double k = 1.0 - dbToLin(dsGr_);
                    l -= (l - lowL) * k; r -= (r - lowR) * k;   // reduce solo la banda alta
                }
            }

            if (hard_.on) hard_.process(l, r);

            l *= outGain_; r *= outGain_;

            // Limitador de seguridad a -1 dBFS (ataque instantaneo, sin latencia)
            const double pk = std::max(std::fabs(l), std::fabs(r));
            const double tg = pk > kCeil ? kCeil / pk : 1.0;
            if (tg < limGain_) limGain_ = tg; else limGain_ = tg + (limGain_ - tg) * limRel_;
            l *= limGain_; r *= limGain_;

            if (!(std::fabs(l) <= 1.0) || !(std::fabs(r) <= 1.0)) { bad = true; l = r = 0.0; }
            outL[i] = (float)l; outR[i] = (float)r;
        }
        if (bad) reset();   // protege contra NaN/Inf que "envenenan" los filtros
    }

    // Para la GUI: curva total del EQ (dB) a una frecuencia, con los parametros de un slot
    static double eqResponseDb(const float* p, double f, double fs = 48000.0) {
        double db = 0.0;
        if (p[SP_HPF] > 21.0f) db += magnitudeDb(highpass(fs, p[SP_HPF], 0.7071), fs, f);
        if (std::fabs(p[SP_B1_G]) > 0.05f) db += magnitudeDb(lowShelf(fs, p[SP_B1_F], p[SP_B1_G]), fs, f);
        if (std::fabs(p[SP_B2_G]) > 0.05f) db += magnitudeDb(peaking(fs, p[SP_B2_F], p[SP_B2_G], p[SP_B2_Q]), fs, f);
        if (std::fabs(p[SP_B3_G]) > 0.05f) db += magnitudeDb(peaking(fs, p[SP_B3_F], p[SP_B3_G], p[SP_B3_Q]), fs, f);
        if (std::fabs(p[SP_B4_G]) > 0.05f) db += magnitudeDb(peaking(fs, p[SP_B4_F], p[SP_B4_G], p[SP_B4_Q]), fs, f);
        if (std::fabs(p[SP_B5_G]) > 0.05f) db += magnitudeDb(highShelf(fs, p[SP_B5_F], p[SP_B5_G]), fs, f);
        return db;
    }

private:
    static constexpr double kCeil = 0.8912509381337456;   // -1 dBFS

    double fs_ = 48000.0;
    bool configured_ = false;
    float cur_[SP_COUNT] = {};

    bool hpfOn_ = false; BiquadCoef hpf_; BiquadState hpfSt_[2];
    bool bandOn_[5] = {false, false, false, false, false};
    BiquadCoef band_[5]; BiquadState bandSt_[2][5];

    bool gateOn_ = false, gateOpen_ = true; int gateHold_ = 0, gateHoldMax_ = 3840;
    double gateEnv_ = 0, gateGain_ = 1, gateOpenThr_ = 0, gateCloseThr_ = 0, gateFloor_ = 0.1;
    double gateEnvAtk_ = 0, gateEnvRel_ = 0, gateAtk_ = 0, gateRel_ = 0;

    Compressor soft_, hard_;

    bool bodyOn_ = false; double bodyMix_ = 0; Shaper bodyShaper_;
    BiquadCoef bodyLp_, bodyHp_; BiquadState bodyLpSt_[2], bodyHpSt_[2];

    static constexpr int    kCrystalStages = 5;      // "los 5 plugins de DeeCrystal"
    static constexpr double kCrystalDrive  = 22.0;   // cuanto satura cada instancia
    static constexpr double kCrystalMix    = 1.3;    // cuanto aporta cada instancia
    int crysN_ = 0; double crysAmt_[kCrystalStages] = {}; Shaper crysShaper_;
    BiquadCoef crysHp1_, crysHp2_; BiquadState crysSt_[kCrystalStages][2][2];   // [etapa][canal][hp1/hp2]

    bool dsOn_ = false; BiquadCoef dsHp_, dsLp_; BiquadState dsSt_[2], dsLpSt_[2];
    double dsThrDb_ = -20, dsGr_ = 0, dsEnv_ = 0, dsEnvRel_ = 0, dsAtk_ = 0, dsRel_ = 0;

    double outGain_ = 1.0, limGain_ = 1.0, limRel_ = 0;
};

} // namespace dsp
