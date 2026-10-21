// gui_win32.cpp - Interfaz de VoiceEQ Mic (solo Windows, GDI puro, sin dependencias externas)
//
// Notas de rendimiento:
//  * No hay medidores ni animaciones. En Equalizer APO el editor es una instancia APARTE que no
//    procesa audio; la ventana solo edita ajustes. Se dibuja unicamente cuando algo cambia.
//  * Un timer de 150 ms solo compara un contador (casi 0 CPU).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <algorithm>
#include "gui.h"
#include "dsp.h"

namespace {

HINSTANCE g_hInst = nullptr;
volatile LONG g_classCounter = 0;

const COLORREF C_BG    = RGB(22, 24, 29);
const COLORREF C_PANEL = RGB(31, 34, 41);
const COLORREF C_GRID  = RGB(52, 57, 67);
const COLORREF C_TEXT  = RGB(214, 219, 229);
const COLORREF C_DIM   = RGB(130, 138, 152);
const COLORREF C_ACC   = RGB(161, 34, 88);

struct NodeDef { int pf, pg, pq; const wchar_t* label; COLORREF col; };
const NodeDef kNodes[6] = {
    {SP_HPF,  -1,      -1,      L"HP", RGB(165, 170, 182)},
    {SP_B1_F, SP_B1_G, -1,      L"1",  RGB(255, 120, 100)},
    {SP_B2_F, SP_B2_G, SP_B2_Q, L"2",  RGB(255, 190, 80)},
    {SP_B3_F, SP_B3_G, SP_B3_Q, L"3",  RGB(130, 220, 120)},
    {SP_B4_F, SP_B4_G, SP_B4_Q, L"4",  RGB(90, 190, 255)},
    {SP_B5_F, SP_B5_G, -1,      L"5",  RGB(190, 140, 255)},
};

struct KnobDef { int sp; const wchar_t* name; };
const int NUM_KNOBS = 7;
// Orden = orden de la cadena de audio (izquierda -> derecha)
const KnobDef kKnobs[NUM_KNOBS] = {
    {SP_GATE,    L"NOISE GATE"},
    {SP_COMP,    L"COMP SOFT"},
    {SP_BODY,    L"CUERPO"},
    {SP_CRYSTAL, L"DEECRYSTAL"},
    {SP_DEESS,   L"DE-ESSER"},
    {SP_COMP2,   L"COMP HARD"},
    {SP_OUT,     L"VOLUMEN"},
};

const wchar_t* const kSlotLabel[NUM_SLOTS] = {L"1  GRAVE", L"2  MEDIA", L"3  AGUDA", L"4  MilsToz"};

// Geometria
const int G_L = 20, G_T = 52, G_R = 680, G_B = 262;
const int G_W = G_R - G_L, G_H = G_B - G_T;
const int KNOB_CY = 338, KNOB_R = 30;
const int SLOT_X0 = 184, SLOT_DX = 106, SLOT_W = 100, SLOT_Y = 12, SLOT_H = 28;
const double kDbRange = 15.0;   // la grafica muestra +-15 dB

struct Gui {
    SharedState* st = nullptr;
    HWND hwnd = nullptr;
    wchar_t cls[64] = {0};
    HFONT fSmall = nullptr, fNorm = nullptr, fBold = nullptr;
    uint32_t lastVersion = 0;
    int dragKind = 0;            // 0 nada, 1 nodo, 2 perilla
    int dragIdx = -1, hover = -1;
    int dragStartY = 0;
    float dragStartNorm = 0.f;

    // ---------- acceso a parametros del slot activo ----------
    int curSlot() const { return st->slot(); }
    float val(int sp) const { return st->getSlotParam(curSlot(), sp); }
    void setVal(int sp, float v) { st->setReal(slotIndex(curSlot(), sp), v); }
    void invalidate() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }

    // ---------- conversiones ----------
    static int freqToX(double f) {
        f = std::min(std::max(f, 20.0), 20000.0);
        return G_L + (int)std::lround(std::log(f / 20.0) / std::log(1000.0) * G_W);
    }
    static double xToFreq(int x) {
        x = std::min(std::max(x, G_L), G_R);
        return 20.0 * std::pow(1000.0, (double)(x - G_L) / G_W);
    }
    static int gainToY(double g) {
        g = std::min(std::max(g, -kDbRange), kDbRange);
        return G_T + G_H / 2 - (int)std::lround(g / kDbRange * (G_H / 2));
    }
    static double yToGain(int y) { return (double)(G_T + G_H / 2 - y) / (G_H / 2) * kDbRange; }
    static int knobCX(int i) { return G_L + G_W / (2 * NUM_KNOBS) + i * (G_W / NUM_KNOBS); }

    void nodePos(int n, int& x, int& y) const {
        const NodeDef& d = kNodes[n];
        x = freqToX(val(d.pf));
        y = d.pg >= 0 ? gainToY(val(d.pg)) : G_B - 14;
    }
    float knobNorm(int k) const {
        const int sp = kKnobs[k].sp;
        return paramToNorm(kInfo[sp], val(sp));
    }
    void applyKnobNorm(int k, float n) {
        n = std::min(std::max(n, 0.f), 1.f);
        const int sp = kKnobs[k].sp;
        float real;
        if (sp == SP_OUT) { real = -12.f + 24.f * n; if (std::fabs(real) < 0.4f) real = 0.f; }
        else { real = n; if (real < 0.02f) real = 0.f; }    // facil de llegar a OFF
        setVal(sp, real);
    }

    // ---------- hit tests ----------
    int hitSlot(int x, int y) const {
        for (int i = 0; i < NUM_SLOTS; ++i) {
            const int l = SLOT_X0 + i * SLOT_DX;
            if (x >= l && x < l + SLOT_W && y >= SLOT_Y && y < SLOT_Y + SLOT_H) return i;
        }
        return -1;
    }
    int hitNode(int mx, int my) const {
        int best = -1; double bd = 13.0 * 13.0;
        for (int n = 0; n < 6; ++n) {
            int x, y; nodePos(n, x, y);
            const double d = (double)(mx - x) * (mx - x) + (double)(my - y) * (my - y);
            if (d <= bd) { bd = d; best = n; }
        }
        return best;
    }
    int hitKnob(int mx, int my) const {
        for (int k = 0; k < NUM_KNOBS; ++k) {
            const int dx = mx - knobCX(k), dy = my - KNOB_CY;
            if (dx * dx + dy * dy <= (KNOB_R + 8) * (KNOB_R + 8)) return k;
        }
        return -1;
    }

    // ---------- eventos ----------
    void resetNode(int n) {
        const NodeDef& d = kNodes[n]; const int s = curSlot();
        setVal(d.pf, kPreset[s][d.pf]);
        if (d.pg >= 0) setVal(d.pg, kPreset[s][d.pg]);
        if (d.pq >= 0) setVal(d.pq, kPreset[s][d.pq]);
    }
    void onDown(int x, int y, bool dbl) {
        const int s = hitSlot(x, y);
        if (s >= 0) { st->setReal(PARAM_SLOT, (float)s); dragKind = 0; invalidate(); return; }
        const int n = hitNode(x, y);
        if (n >= 0) {
            hover = n;
            if (dbl) resetNode(n); else { dragKind = 1; dragIdx = n; }
            invalidate(); return;
        }
        const int k = hitKnob(x, y);
        if (k >= 0) {
            if (dbl) setVal(kKnobs[k].sp, kPreset[curSlot()][kKnobs[k].sp]);
            else { dragKind = 2; dragIdx = k; dragStartY = y; dragStartNorm = knobNorm(k); }
            invalidate();
        }
    }
    void onMove(int x, int y) {
        if (dragKind == 1) {
            const NodeDef& d = kNodes[dragIdx];
            setVal(d.pf, (float)xToFreq(x));
            if (d.pg >= 0) {
                double g = yToGain(y);
                if (std::fabs(g) < 0.25) g = 0.0;
                setVal(d.pg, (float)g);
            }
            invalidate();
        } else if (dragKind == 2) {
            applyKnobNorm(dragIdx, dragStartNorm + (float)(dragStartY - y) / 180.f);
            invalidate();
        } else {
            const int h = hitNode(x, y);
            if (h != hover) { hover = h; invalidate(); }
        }
    }
    void onUp() { dragKind = 0; dragIdx = -1; invalidate(); }
    void onWheel(int x, int y, int delta) {
        const int n = hitNode(x, y);
        if (n >= 0 && kNodes[n].pq >= 0) {
            setVal(kNodes[n].pq, val(kNodes[n].pq) * std::pow(1.12f, (float)delta / 120.f));
            invalidate(); return;
        }
        const int k = hitKnob(x, y);
        if (k >= 0) { applyKnobNorm(k, knobNorm(k) + 0.02f * (float)delta / 120.f); invalidate(); }
    }

    // ---------- dibujo ----------
    void createFonts() {
        fSmall = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        fNorm  = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        fBold  = CreateFontW(-18, 0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }
    void destroyFonts() {
        if (fSmall) DeleteObject(fSmall);
        if (fNorm) DeleteObject(fNorm);
        if (fBold) DeleteObject(fBold);
        fSmall = fNorm = fBold = nullptr;
    }

    static void fillRect(HDC dc, int l, int t, int r, int b, COLORREF c) {
        RECT rc = {l, t, r, b};
        HBRUSH br = CreateSolidBrush(c); FillRect(dc, &rc, br); DeleteObject(br);
    }
    static void line(HDC dc, int x1, int y1, int x2, int y2, COLORREF c, int w = 1) {
        HPEN pen = CreatePen(PS_SOLID, w, c); HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2);
        SelectObject(dc, old); DeleteObject(pen);
    }
    static void roundRect(HDC dc, int l, int t, int r, int b, COLORREF fill, COLORREF border, int rad) {
        HBRUSH br = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
        RoundRect(dc, l, t, r, b, rad, rad);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(br); DeleteObject(pen);
    }
    static void circle(HDC dc, int cx, int cy, int r, COLORREF fill, COLORREF border, int bw = 1) {
        HBRUSH br = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, bw, border);
        HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(br); DeleteObject(pen);
    }
    static void arc(HDC dc, int cx, int cy, int r, double a0, double a1, COLORREF col, int w) {
        POINT pts[64];
        int n = std::max(2, (int)((a1 - a0) / 5.0) + 1);
        if (n > 64) n = 64;
        for (int i = 0; i < n; ++i) {
            const double a = (a0 + (a1 - a0) * i / (n - 1)) * dsp::kPi / 180.0;
            pts[i].x = cx + (LONG)std::lround(r * std::sin(a));
            pts[i].y = cy - (LONG)std::lround(r * std::cos(a));
        }
        HPEN pen = CreatePen(PS_SOLID, w, col); HGDIOBJ old = SelectObject(dc, pen);
        Polyline(dc, pts, n);
        SelectObject(dc, old); DeleteObject(pen);
    }
    static void text(HDC dc, HFONT f, int x, int y, const wchar_t* s, COLORREF c, UINT align) {
        SelectObject(dc, f); SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
        SetTextAlign(dc, align | TA_TOP);
        TextOutW(dc, x, y, s, (int)wcslen(s));
    }

    void drawAll(HDC dc) {
        const int slot = curSlot();
        fillRect(dc, 0, 0, GUI_W, GUI_H, C_BG);

        // --- titulo y slots
        text(dc, fBold, 20, 10, L"MilsToz Mic", C_TEXT, TA_LEFT);
        text(dc, fSmall, 20, 32, L"voz + mic economico", C_DIM, TA_LEFT);
        for (int i = 0; i < NUM_SLOTS; ++i) {
            const int l = SLOT_X0 + i * SLOT_DX;
            const bool sel = (i == slot);
            roundRect(dc, l, SLOT_Y, l + SLOT_W, SLOT_Y + SLOT_H, sel ? C_ACC : C_PANEL, sel ? C_ACC : C_GRID, 8);
            text(dc, fNorm, l + SLOT_W / 2, SLOT_Y + 6, kSlotLabel[i], sel ? RGB(10, 18, 28) : C_TEXT, TA_CENTER);
        }

        // --- panel de la grafica
        roundRect(dc, G_L - 6, G_T - 6, G_R + 6, G_B + 6, C_PANEL, C_GRID, 10);
        const double fl[8] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
        const wchar_t* fn[8] = {L"50", L"100", L"200", L"500", L"1k", L"2k", L"5k", L"10k"};
        for (int i = 0; i < 8; ++i) {
            const int x = freqToX(fl[i]);
            line(dc, x, G_T, x, G_B, C_GRID);
            text(dc, fSmall, x, G_B - 14, fn[i], C_DIM, TA_CENTER);
        }
        const int dl[5] = {-12, -6, 0, 6, 12};
        const wchar_t* dn[5] = {L"-12", L"-6", L"0", L"+6", L"+12"};
        for (int i = 0; i < 5; ++i) {
            const int y = gainToY(dl[i]);
            line(dc, G_L, y, G_R, y, dl[i] == 0 ? C_DIM : C_GRID);
            text(dc, fSmall, G_L + 3, y - 13, dn[i], C_DIM, TA_LEFT);
        }

        // --- curva total del EQ
        float p[SP_COUNT];
        for (int i = 0; i < SP_COUNT; ++i) p[i] = st->getSlotParam(slot, i);
        POINT pts[300]; int np = 0;
        for (int x = 0; x <= G_W && np < 300; x += 2) {
            const double d = dsp::VoiceEngine::eqResponseDb(p, xToFreq(G_L + x), 48000.0);
            pts[np].x = G_L + x; pts[np].y = gainToY(d); ++np;
        }
        HPEN pen = CreatePen(PS_SOLID, 2, C_ACC); HGDIOBJ old = SelectObject(dc, pen);
        Polyline(dc, pts, np);
        SelectObject(dc, old); DeleteObject(pen);

        // --- nodos
        const int active = (dragKind == 1) ? dragIdx : hover;
        for (int n = 0; n < 6; ++n) {
            int x, y; nodePos(n, x, y);
            const bool a = (n == active);
            circle(dc, x, y, a ? 11 : 9, kNodes[n].col, a ? RGB(255, 255, 255) : RGB(20, 22, 26), a ? 2 : 1);
            text(dc, fSmall, x, y - 8, kNodes[n].label, RGB(15, 17, 21), TA_CENTER);
        }

        // --- linea de informacion
        wchar_t b[128];
        if (active >= 0) {
            const NodeDef& d = kNodes[active];
            if (active == 0) {
                _snwprintf(b, 127, L"Filtro pasa-altos   %d Hz", (int)(val(d.pf) + 0.5f));
            } else if (d.pq >= 0) {
                _snwprintf(b, 127, L"Banda %d    %d Hz    %+.1f dB    Q %.2f", active, (int)(val(d.pf) + 0.5f), val(d.pg), val(d.pq));
            } else {
                _snwprintf(b, 127, L"Banda %d    %d Hz    %+.1f dB", active, (int)(val(d.pf) + 0.5f), val(d.pg));
            }
            b[127] = 0;
            text(dc, fNorm, G_L, G_B + 14, b, C_TEXT, TA_LEFT);
        } else {
            text(dc, fSmall, G_L, G_B + 16, L"Arrastra los puntos: izquierda/derecha = frecuencia, arriba/abajo = ganancia.  Rueda = Q.  Doble clic = restablecer.", C_DIM, TA_LEFT);
        }

        // --- perillas
        for (int k = 0; k < NUM_KNOBS; ++k) {
            const int cx = knobCX(k), cy = KNOB_CY, sp = kKnobs[k].sp;
            const float n = knobNorm(k), v = val(sp);
            arc(dc, cx, cy, KNOB_R, -135.0, 135.0, C_GRID, 5);
            if (n > 0.001f) arc(dc, cx, cy, KNOB_R, -135.0, -135.0 + 270.0 * n, C_ACC, 5);
            circle(dc, cx, cy, KNOB_R - 9, C_PANEL, C_GRID);
            const double a = (-135.0 + 270.0 * n) * dsp::kPi / 180.0;
            line(dc, cx + (int)std::lround(5 * std::sin(a)), cy - (int)std::lround(5 * std::cos(a)),
                     cx + (int)std::lround(19 * std::sin(a)), cy - (int)std::lround(19 * std::cos(a)), C_TEXT, 2);
            text(dc, fNorm, cx, cy + KNOB_R + 8, kKnobs[k].name, C_TEXT, TA_CENTER);
            bool off = false;
            if (sp == SP_OUT) _snwprintf(b, 127, L"%+.1f dB", v);
            else if (v < 0.005f) { _snwprintf(b, 127, L"OFF"); off = true; }
            else _snwprintf(b, 127, L"%d%%", (int)(v * 100.f + 0.5f));
            b[127] = 0;
            text(dc, fNorm, cx, cy + KNOB_R + 26, b, off ? C_DIM : C_ACC, TA_CENTER);
        }
    }

    void paint(HDC hdc) {
        lastVersion = st->version.load(std::memory_order_acquire);
        HDC mdc = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, GUI_W, GUI_H);
        HGDIOBJ oldBmp = SelectObject(mdc, bmp);
        drawAll(mdc);
        BitBlt(hdc, 0, 0, GUI_W, GUI_H, mdc, 0, 0, SRCCOPY);
        SelectObject(mdc, oldBmp); DeleteObject(bmp); DeleteDC(mdc);
    }
};

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        Gui* gg = static_cast<Gui*>(cs->lpCreateParams);
        if (gg) gg->hwnd = h;
        return DefWindowProcW(h, m, w, l);
    }
    Gui* g = reinterpret_cast<Gui*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!g) return DefWindowProcW(h, m, w, l);

    switch (m) {
        case WM_CREATE: g->createFonts(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
            g->paint(hdc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetFocus(h); SetCapture(h);
            g->onDown(GET_X_LPARAM(l), GET_Y_LPARAM(l), false);
            return 0;
        case WM_LBUTTONDBLCLK:
            g->onDown(GET_X_LPARAM(l), GET_Y_LPARAM(l), true);
            return 0;
        case WM_MOUSEMOVE:
            g->onMove(GET_X_LPARAM(l), GET_Y_LPARAM(l));
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == h) ReleaseCapture();
            g->onUp();
            return 0;
        case WM_CAPTURECHANGED:
            g->dragKind = 0;
            return 0;
        case WM_MOUSEWHEEL: {
            POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &pt);
            g->onWheel(pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(w));
            return 0;
        }
        case WM_TIMER:   // por si el host cambia un parametro mientras la ventana esta abierta
            if (w == 1 && g->st->version.load(std::memory_order_acquire) != g->lastVersion) g->invalidate();
            return 0;
        case WM_DESTROY:
            KillTimer(h, 1);
            g->destroyFonts();
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_hInst = h; DisableThreadLibraryCalls(h); }
    return TRUE;
}

void* guiOpen(SharedState* st, void* parentWnd) {
    Gui* g = new Gui();
    g->st = st;
    // Nombre de clase unico por ventana: se registra al abrir y se borra al cerrar,
    // asi nunca queda una clase "huerfana" apuntando a codigo de la DLL ya descargada.
    const LONG id = InterlockedIncrement(&g_classCounter);
    _snwprintf(g->cls, 63, L"VoiceEQMicGUI_%ld", (long)id);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.lpszClassName = g->cls;
    if (!RegisterClassExW(&wc)) { delete g; return nullptr; }

    g->hwnd = CreateWindowExW(0, g->cls, L"MilsToz Mic", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                          0, 0, GUI_W, GUI_H, static_cast<HWND>(parentWnd), nullptr, g_hInst, g);
    if (!g->hwnd) { UnregisterClassW(g->cls, g_hInst); delete g; return nullptr; }
    SetTimer(g->hwnd, 1, 150, nullptr);
    return g;
}

void guiClose(void* p) {
    Gui* g = static_cast<Gui*>(p);
    if (!g) return;
    if (g->hwnd && IsWindow(g->hwnd)) DestroyWindow(g->hwnd);
    UnregisterClassW(g->cls, g_hInst);
    delete g;
}
