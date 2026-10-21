// test_dsp.cpp - verifica el motor de audio de forma numerica
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <chrono>
#include "../src/dsp.h"

using namespace dsp;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; std::printf("  FALLA: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const double FS = 48000.0;

static void flat(float* p) {                 // todo apagado / plano
    for (int i = 0; i < SP_COUNT; ++i) p[i] = 0.f;
    p[SP_HPF] = 20.f; p[SP_B1_F] = 120.f; p[SP_B2_F] = 300.f; p[SP_B2_Q] = 1.f; p[SP_B3_F] = 1000.f; p[SP_B3_Q] = 1.f;
    p[SP_B4_F] = 3000.f; p[SP_B4_Q] = 1.f; p[SP_B5_F] = 10000.f;
}
static double rms(const std::vector<float>& v, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b; ++i) s += (double)v[i] * v[i];
    return std::sqrt(s / (double)(b - a));
}
static double db(double x) { return 20 * std::log10(x + 1e-12); }

static std::vector<float> run(VoiceEngine& e, const std::vector<float>& x) {
    std::vector<float> y(x.size());
    const float* in[2] = {x.data(), x.data()};
    float* out[2] = {y.data(), y.data()};
    std::vector<float> y2(x.size()); out[1] = y2.data();
    // en bloques de 480 (10 ms) como lo haria Windows
    for (size_t i = 0; i < x.size(); i += 480) {
        const int n = (int)std::min<size_t>(480, x.size() - i);
        const float* ib[2] = {in[0] + i, in[1] + i};
        float* ob[2] = {out[0] + i, out[1] + i};
        e.process(ib, ob, n);
    }
    return y;
}
static std::vector<float> sine(double f, double amp, double sec) {
    std::vector<float> v((size_t)(sec * FS));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float)(amp * std::sin(2 * kPi * f * i / FS));
    return v;
}

static void testEq() {
    std::printf("[EQ] respuesta medida vs. teorica (preset 'Media')\n");
    VoiceEngine e; e.setSampleRate(FS);
    float p[SP_COUNT]; for (int i = 0; i < SP_COUNT; ++i) p[i] = kPreset[1][i];
    p[SP_COMP] = 0; p[SP_DEESS] = 0; p[SP_GATE] = 0;       // solo EQ
    e.configure(p);
    const double freqs[] = {60, 150, 300, 1000, 3200, 10000};
    for (double f : freqs) {
        e.reset();
        auto y = run(e, sine(f, 0.05, 0.6));
        const double meas = db(rms(y, 12000, y.size())) - db(0.05 / std::sqrt(2.0));
        const double theo = VoiceEngine::eqResponseDb(p, f, FS);
        std::printf("   %6.0f Hz  medido %+6.2f dB   teorico %+6.2f dB\n", f, meas, theo);
        CHECK(std::fabs(meas - theo) < 0.1, "EQ a %.0f Hz difiere %.3f dB", f, meas - theo);
    }
}

static std::vector<float> noiseSpeech() {
    // 1 s ruido (~-52 dBFS rms) | 1 s "voz" (-20 dBFS) + ruido | 1 s ruido
    std::vector<float> x((size_t)(3 * FS));
    unsigned s = 12345;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return ((s >> 8) / 8388608.0) - 1.0; };
    for (size_t i = 0; i < x.size(); ++i) {
        double t = i / FS, n = 0.0025 * rnd() * 1.7;       // ~ -52 dBFS rms
        double v = 0;
        if (t >= 1.0 && t < 2.0) v = 0.10 * (std::sin(2 * kPi * 180 * t) + 0.5 * std::sin(2 * kPi * 360 * t) +
                                             0.3 * std::sin(2 * kPi * 900 * t)) * (0.6 + 0.4 * std::sin(2 * kPi * 4 * t));
        x[i] = (float)(n + v);
    }
    return x;
}

static void testGate() {
    std::printf("[GATE] apagado por defecto y efectivo al mover la perilla\n");
    auto x = noiseSpeech();
    float p[SP_COUNT]; flat(p);
    VoiceEngine e; e.setSampleRate(FS);

    e.configure(p); auto y0 = run(e, x);
    const double n0 = db(rms(y0, (size_t)(2.5 * FS), (size_t)(3 * FS)));
    const double nin = db(rms(x, (size_t)(2.5 * FS), (size_t)(3 * FS)));
    std::printf("   gate=0   ruido final: in %.1f dBFS -> out %.1f dBFS\n", nin, n0);
    CHECK(std::fabs(n0 - nin) < 0.1, "con la perilla en 0 el audio debe pasar intacto");

    p[SP_GATE] = 0.7f; e.reset(); e.configure(p); auto y1 = run(e, x);
    const double n1 = db(rms(y1, (size_t)(2.5 * FS), (size_t)(3 * FS)));
    const double v0 = db(rms(x, (size_t)(1.2 * FS), (size_t)(1.9 * FS)));
    const double v1 = db(rms(y1, (size_t)(1.2 * FS), (size_t)(1.9 * FS)));
    std::printf("   gate=70%% ruido final: %.1f dBFS (baja %.1f dB) | voz: %.1f -> %.1f dBFS\n", n1, nin - n1, v0, v1);
    CHECK(nin - n1 > 15.0, "el gate debe bajar el ruido >15 dB (bajo %.1f)", nin - n1);
    CHECK(std::fabs(v1 - v0) < 1.0, "la voz no debe cambiar (>1 dB): %.2f", v1 - v0);

    // mover la perilla EN VIVO (sin reset) debe tener efecto
    e.reset(); p[SP_GATE] = 0; e.configure(p); run(e, x);
    p[SP_GATE] = 0.7f; e.configure(p); auto y2 = run(e, x);
    const double n2 = db(rms(y2, (size_t)(2.5 * FS), (size_t)(3 * FS)));
    CHECK(nin - n2 > 15.0, "mover la perilla en vivo debe activar el gate");
}

static void testComp() {
    std::printf("[COMP] reduce la diferencia entre voz suave y fuerte\n");
    std::vector<float> x;
    auto a = sine(300, 0.03, 0.8), b = sine(300, 0.30, 0.8);       // -30 dBFS y -10 dBFS
    x.insert(x.end(), a.begin(), a.end()); x.insert(x.end(), b.begin(), b.end());
    float p[SP_COUNT]; flat(p); VoiceEngine e; e.setSampleRate(FS);
    e.configure(p); auto y0 = run(e, x);
    const double d0 = db(rms(y0, (size_t)(1.2 * FS), (size_t)(1.6 * FS))) - db(rms(y0, (size_t)(0.5 * FS), (size_t)(0.8 * FS)));
    p[SP_COMP] = 0.6f; e.reset(); e.configure(p); auto y1 = run(e, x);
    const double d1 = db(rms(y1, (size_t)(1.2 * FS), (size_t)(1.6 * FS))) - db(rms(y1, (size_t)(0.5 * FS), (size_t)(0.8 * FS)));
    std::printf("   diferencia fuerte-suave: %.1f dB (sin comp) -> %.1f dB (comp 60%%)\n", d0, d1);
    CHECK(std::fabs(d0 - 20.0) < 0.2, "sin compresor la diferencia debe ser 20 dB");
    CHECK(d1 < d0 - 3.0, "el compresor debe reducir la diferencia al menos 3 dB");
}

// amplitud (pico equivalente) de la componente de frecuencia f en v[a..fin)
static double tone(const std::vector<float>& v, size_t a, double f) {
    double re = 0, im = 0;
    for (size_t i = a; i < v.size(); ++i) { const double w = 2 * kPi * f * i / FS; re += v[i] * std::cos(w); im += v[i] * std::sin(w); }
    return 2.0 * std::sqrt(re * re + im * im) / (double)(v.size() - a);
}

static void testComp2() {
    std::printf("[COMP HARD] comprime mas fuerte que el soft con la misma perilla, y 0 = apagado\n");
    std::vector<float> x;
    auto a = sine(300, 0.03, 0.8), b = sine(300, 0.30, 0.8);
    x.insert(x.end(), a.begin(), a.end()); x.insert(x.end(), b.begin(), b.end());
    auto diff = [&](const std::vector<float>& y) {
        return db(rms(y, (size_t)(1.2 * FS), (size_t)(1.6 * FS))) - db(rms(y, (size_t)(0.5 * FS), (size_t)(0.8 * FS)));
    };
    float p[SP_COUNT]; flat(p); VoiceEngine e; e.setSampleRate(FS);
    e.configure(p); auto y0 = run(e, x);
    p[SP_COMP] = 0.6f; e.reset(); e.configure(p); auto ys = run(e, x);
    p[SP_COMP] = 0.f; p[SP_COMP2] = 0.6f; e.reset(); e.configure(p); auto yh = run(e, x);
    std::printf("   diferencia fuerte-suave: %.1f dB (sin comp) | soft 60%%: %.1f dB | hard 60%%: %.1f dB\n", diff(y0), diff(ys), diff(yh));
    CHECK(std::fabs(diff(y0) - 20.0) < 0.2, "con las perillas en 0 la diferencia debe ser 20 dB");
    CHECK(diff(yh) < diff(ys) - 2.0, "el hard debe comprimir al menos 2 dB mas que el soft");
    p[SP_COMP] = 0.6f; p[SP_COMP2] = 0.6f; e.reset(); e.configure(p); auto yb = run(e, x);
    std::printf("   los dos juntos (60%% + 60%%): %.1f dB\n", diff(yb));
    CHECK(diff(yb) < diff(yh), "soft + hard juntos deben comprimir mas que el hard solo");
}

static void testCrystal() {
    std::printf("[DEECRYSTAL] una perilla 0-100%% = de 0 a 5 instancias apiladas (mas armonicos y brillo, sin tocar la voz)\n");
    float p[SP_COUNT]; flat(p); VoiceEngine e; e.setSampleRate(FS);
    const double f0 = 4500.0, amp = 0.03;                              // -30 dBFS: nivel de voz normal
    const double base = tone(run(e, sine(f0, amp, 0.5)), 12000, f0);   // referencia sin efecto (e sin configurar)
    double prevHarm = -1; bool mono = true;
    for (int k = 0; k <= 5; ++k) {
        const float a = k / 5.0f;
        p[SP_CRYSTAL] = a; e.configure(p); e.reset();
        auto y = run(e, sine(f0, amp, 0.5));
        const double fund = tone(y, 12000, f0), h2 = tone(y, 12000, 2 * f0), h3 = tone(y, 12000, 3 * f0);
        const double harm = db(std::sqrt(h2 * h2 + h3 * h3)), gain = db(fund) - db(base);
        std::printf("   %3d%% (%d inst.)  fundamental %+5.2f dB | armonicos 9k+13.5k: %7.1f dBFS\n", (int)(a * 100 + 0.5f), k, gain, harm);
        if (k > 0 && harm < prevHarm + 1.0) mono = false;           // cada instancia suma >= 1 dB de armonicos
        if (k == 0) CHECK(std::fabs(gain) < 0.05 && harm < -100.0, "en 0%% no debe cambiar nada");
        CHECK(std::fabs(gain) < 1.0, "la fundamental no debe cambiar mas de 1 dB (%.2f)", gain);
        prevHarm = harm;
    }
    CHECK(mono, "mas perilla debe dar siempre mas armonicos");
    CHECK(prevHarm > -60.0, "al 100%% los armonicos deben notarse (> -60 dBFS), medido %.1f", prevHarm);
    p[SP_CRYSTAL] = 1.f; e.configure(p); e.reset();
    auto v = run(e, sine(1000, 0.25, 0.5));
    const double chg = db(rms(v, 12000, v.size())) - db(0.25 / std::sqrt(2.0));
    std::printf("   al 100%%, un tono de voz de 1 kHz cambia %.2f dB\n", chg);
    CHECK(std::fabs(chg) < 0.3, "DeeCrystal no debe tocar la zona de la voz (1 kHz): %.2f dB", chg);
}

static void testBody() {
    std::printf("[CUERPO] agrega armonicos a los graves, no toca los agudos\n");
    float p[SP_COUNT]; flat(p); VoiceEngine e; e.setSampleRate(FS);
    e.configure(p); auto y0 = run(e, sine(100, 0.3, 0.6));
    p[SP_BODY] = 0.8f; e.configure(p); e.reset(); auto y1 = run(e, sine(100, 0.3, 0.6));
    const double h0 = db(tone(y0, 12000, 200) + tone(y0, 12000, 300)), h1 = db(tone(y1, 12000, 200) + tone(y1, 12000, 300));
    std::printf("   100 Hz: armonicos (200+300 Hz) %.1f -> %.1f dBFS\n", h0, h1);
    CHECK(h1 > h0 + 20.0, "debe generar armonicos (>20 dB mas)");
    e.reset(); auto t = run(e, sine(3000, 0.25, 0.5));
    const double chg = db(rms(t, 12000, t.size())) - db(0.25 / std::sqrt(2.0));
    CHECK(std::fabs(chg) < 0.3, "3 kHz no debe cambiar (%.2f dB)", chg);
}

static void testDeess() {
    std::printf("[DE-ESS] baja sibilancia (7 kHz) y no toca la voz (1 kHz)\n");
    float p[SP_COUNT]; flat(p); VoiceEngine e; e.setSampleRate(FS);
    auto hi = sine(7000, 0.25, 0.8), lo = sine(1000, 0.25, 0.8);
    e.configure(p); auto h0 = run(e, hi);
    p[SP_DEESS] = 0.6f; e.reset(); e.configure(p); auto h1 = run(e, hi); e.reset(); auto l1 = run(e, lo);
    const double red = db(rms(h0, 24000, h0.size())) - db(rms(h1, 24000, h1.size()));
    const double lo_chg = db(rms(l1, 24000, l1.size())) - db(0.25 / std::sqrt(2.0));
    std::printf("   7 kHz reducido %.1f dB | 1 kHz cambia %.2f dB\n", red, lo_chg);
    CHECK(red > 3.0, "debe reducir 7 kHz >3 dB");
    CHECK(std::fabs(lo_chg) < 0.5, "no debe tocar 1 kHz");
}

static void testLimiterAndSafety() {
    std::printf("[SEGURIDAD] limitador y proteccion contra NaN\n");
    float p[SP_COUNT]; flat(p); p[SP_OUT] = 12.f; VoiceEngine e; e.setSampleRate(FS); e.configure(p);
    auto y = run(e, sine(500, 0.9, 0.5));
    float pk = 0; for (float v : y) pk = std::max(pk, std::fabs(v));
    std::printf("   +12 dB sobre 0.9 de entrada -> pico de salida %.4f (techo 0.8913)\n", pk);
    CHECK(pk <= 0.8913f + 1e-4f, "el limitador debe respetar el techo");

    std::vector<float> bad(4800, 0.1f); bad[100] = NAN; bad[200] = INFINITY;
    e.reset(); run(e, bad);
    auto good = run(e, sine(500, 0.1, 0.2));
    bool ok = true; for (float v : good) if (!std::isfinite(v)) ok = false;
    CHECK(ok, "tras NaN/Inf la salida debe volver a ser finita");
    CHECK(rms(good, 4800, good.size()) > 0.01, "tras NaN/Inf el audio debe seguir pasando");
}

static void testCpu() {
    std::printf("[CPU] TODO activado (preset MilsToz + gate + comp soft + comp hard + cuerpo + crystal + de-ess)\n");
    float p[SP_COUNT]; for (int i = 0; i < SP_COUNT; ++i) p[i] = kPreset[3][i];
    p[SP_GATE] = 0.5f; p[SP_COMP] = 0.8f; p[SP_DEESS] = 0.6f;
    p[SP_COMP2] = 0.8f; p[SP_CRYSTAL] = 1.0f; p[SP_BODY] = 0.8f;
    VoiceEngine e; e.setSampleRate(FS); e.configure(p);
    auto x = noiseSpeech();
    const double audioSec = 3.0 * 20;                       // 60 s de audio estereo
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < 20; ++r) run(e, x);
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("   %.0f s de audio en %.3f s  => %.2f %% de UN nucleo, %.0f x tiempo real\n",
                audioSec, sec, 100.0 * sec / audioSec, audioSec / sec);
    CHECK(100.0 * sec / audioSec < 5.0, "uso de CPU demasiado alto");
}

int main() {
    testEq(); testGate(); testComp(); testComp2(); testCrystal(); testBody(); testDeess(); testLimiterAndSafety(); testCpu();
    std::printf("\n%s (%d fallas)\n", fails ? "RESULTADO: HAY FALLAS" : "RESULTADO: TODO OK", fails);
    return fails ? 1 : 0;
}
