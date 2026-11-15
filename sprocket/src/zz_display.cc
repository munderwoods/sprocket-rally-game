// The window's mode, monitor and icon, through the platform's own calls. This file is last
// in the unity build on purpose: nothing after it sees what <windows.h> defines.
#include "common.h"
#include "ui.h"
#include "icon_data.h"

// A keyboard that also reports itself as a gamepad (analog keyboards do) is not a pad to
// hand to a player: told by its name, which only SDL knows. Declared here rather than
// included, since only this one call is wanted on every platform.
#if defined(_WIN32) || defined(__linux__)
extern "C" const char* SDL_GetGamepadName(struct SDL_Gamepad* gamepad);
namespace sr {
bool pad_is_keyboard(void* sdl_gamepad) {
    const char* name = sdl_gamepad ? SDL_GetGamepadName((struct SDL_Gamepad*)sdl_gamepad) : nullptr;
    if (!name) return false;
    auto has = [&](const char* word) {
        for (const char* p = name; *p; ++p) {
            int i = 0;
            while (word[i] && p[i] && ((p[i] | 32) == (word[i] | 32))) ++i;
            if (!word[i]) return true;
        }
        return false;
    };
    return has("wooting") || has("keyboard") || has("keypad");
}
const char* pad_name(void* sdl_gamepad) {
    const char* name = sdl_gamepad ? SDL_GetGamepadName((struct SDL_Gamepad*)sdl_gamepad) : nullptr;
    return name ? name : "GAMEPAD";
}
} // namespace sr
#else
namespace sr { bool pad_is_keyboard(void*) { return false; } const char* pad_name(void*) { return "GAMEPAD"; } }
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef near
#undef far
#undef small

namespace sr {

namespace {
struct Monitors { RECT r[8]; int n; };
BOOL CALLBACK monitor_cb(HMONITOR, HDC, LPRECT rc, LPARAM p) {
    Monitors* m = (Monitors*)p;
    if (m->n < 8) m->r[m->n++] = *rc;
    return TRUE;
}
Monitors monitors() {
    Monitors m{};
    EnumDisplayMonitors(nullptr, nullptr, monitor_cb, (LPARAM)&m);
    return m;
}
} // namespace

int display_count(void*) { return monitors().n; }

void display_apply(void* native, int mode, int monitor) {
    HWND h = (HWND)native;
    if (!h) return;
    Monitors m = monitors();
    if (m.n == 0) return;
    if (monitor < 0 || monitor >= m.n) monitor = 0;
    RECT r = m.r[monitor];
    int mw = r.right - r.left, mh = r.bottom - r.top;
    if (mode == 0) {
        // a 4:3 window as tall as suits the monitor, centred on it
        int ch = mh >= 1200 ? 960 : (mh * 3 / 4) / 224 * 224;
        if (ch < 448) ch = 448;
        RECT w{0, 0, ch * 4 / 3, ch};
        AdjustWindowRect(&w, WS_OVERLAPPEDWINDOW, FALSE);
        int ww = w.right - w.left, wh = w.bottom - w.top;
        SetWindowLongPtrW(h, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(h, HWND_NOTOPMOST, r.left + (mw - ww) / 2, r.top + (mh - wh) / 2, ww, wh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        // no frame, the whole monitor. Fullscreen also stays above the taskbar and other
        // windows (this renderer has no exclusive mode; covering the monitor gives the same
        // direct presentation).
        SetWindowLongPtrW(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(h, mode == 2 ? HWND_TOPMOST : HWND_NOTOPMOST, r.left, r.top, mw, mh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

void display_set_icon(void* native) {
    HWND h = (HWND)native;
    if (!h) return;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HICON big = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
    HICON sm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (big) SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)big);
    if (sm) SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)sm);
}

} // namespace sr

#elif defined(__linux__)
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

namespace sr {

int display_count(void*) {
    int n = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    SDL_free(ids);
    return n;
}

void display_apply(void* native, int mode, int monitor) {
    SDL_Window* w = (SDL_Window*)native;
    if (!w) return;
    int n = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    if (!ids || n == 0) { SDL_free(ids); return; }
    if (monitor < 0 || monitor >= n) monitor = 0;
    SDL_DisplayID id = ids[monitor];
    SDL_free(ids);
    SDL_Rect r{};
    SDL_GetDisplayBounds(id, &r);
    SDL_SetWindowFullscreen(w, false);
    if (mode == 0) {
        SDL_SetWindowBordered(w, true);
        SDL_SetWindowSize(w, 1280, 960);
        SDL_SetWindowPosition(w, (int)SDL_WINDOWPOS_CENTERED_DISPLAY(id), (int)SDL_WINDOWPOS_CENTERED_DISPLAY(id));
    } else if (mode == 1) {
        SDL_SetWindowBordered(w, false);
        SDL_SetWindowPosition(w, r.x, r.y);
        SDL_SetWindowSize(w, r.w, r.h);
    } else {
        SDL_SetWindowPosition(w, r.x, r.y); // onto that monitor, then fullscreen there
        SDL_SyncWindow(w);
        SDL_SetWindowFullscreen(w, true);
    }
}

void display_set_icon(void* native) {
    SDL_Window* w = (SDL_Window*)native;
    if (!w) return;
    SDL_Surface* s = SDL_CreateSurfaceFrom(32, 32, SDL_PIXELFORMAT_RGBA32, (void*)k_icon32, 32 * 4);
    if (!s) return;
    SDL_SetWindowIcon(w, s);
    SDL_DestroySurface(s);
}

} // namespace sr

#else

namespace sr {
int  display_count(void*) { return 1; }
void display_apply(void*, int, int) {}
void display_set_icon(void*) {}
} // namespace sr

#endif
