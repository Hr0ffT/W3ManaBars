// W3ManaBars.mix - mana bars under the unit health bars for Warcraft III 1.26a, 1.27b and 1.28.5
// Copyright (c) 2026 Hr0ffT - MIT License - https://github.com/Hr0ffT
// Build: i686-w64-mingw32-gcc -O2 -Wall -shared -static-libgcc -s -o W3ManaBars.mix W3ManaBars.c -lversion
//
// The unit health bars are CStatBar frames. A CStatBar has a type: 5 is the small health bar over units, 4 the small
// mana bar (SimpleManaBarConsoleSmall, a blue fill showing the unit's mana), which the game itself never puts over
// units. Every frame the game binds each shown health bar to its unit (CStatBar::SetUnit, vtable +0x74) and places it.
// Here, when a health bar of a unit with a mana pool is bound, a type 4 bar is made as its child, anchored under it
// across its width and bound to the same unit. As a child it shows and hides with the health bar and follows it
// wherever it goes, with the frame level of the health bar (W3TrueWidescreen draws those under the console).
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef uint32_t u32;
static u32 g_base;
static FILE* g_log;
static int g_debug;
static float g_heightPct = 100.0f;
static u32 g_color = 0xFF2F6BFF;   // ManaColor (RRGGBB): the game's own mana blue (0000FF) is hard to tell from the black of the empty part
static u32 g_nHide, g_nShow, g_nLevel, g_nMake, g_nCall; static DWORD g_diagTick;

typedef struct {
    u32 build;
    u32 vtStatBar;           // CStatBar vtable
    u32 ctor;                // CStatBar::CStatBar(parent, unit, type), thiscall
    u32 alloc, allocFile;    // the game's allocator (size, file, line, flags), stdcall, and the CStatBar source file name
    u32 setFramePoint;       // thiscall(point, relativeTo, relPoint, x, y, flag)
    u32 setHeight;           // thiscall(height)
    u32 maxMana;             // thiscall on unit+0xB8 (out float*): the unit's maximum mana
    u32 setColor;            // thiscall on the bar's fill texture (+0x134), (const u32* argb)
} Addrs;
static const Addrs kAddrs[] = {
    { 6401, 0x93E604, 0x359CC0, 0x6EB5B2, 0x940968, 0x606770, 0x605DB0, 0x4773E0, 0x60E740 },   // 1.26a
    { 7085, 0xABCE78, 0x3A16E0, 0x07C6D2, 0xABBA18, 0x111600, 0x111520, 0x058040, 0x113790 },   // 1.27b
    { 7680, 0xAAD83C, 0x3D57F0, 0x0A5FE8, 0xAAC3C8, 0x13FC20, 0x13FB40, 0x081BA0, 0x141E40 },   // 1.28.5
};
static const Addrs* A;

#define OFS_PARENT 0x6C
#define OFS_LEVEL  0x84
#define OFS_WANT   0x90
#define OFS_SHOWN  0x94
#define OFS_TYPE   0x138
#define SB_SIZE    0x158
#define TYPE_HP_SMALL   5
#define TYPE_MANA_SMALL 4

static void logf_(const char* f, ...)
{
    if (!g_log) return;
    va_list a; va_start(a, f); vfprintf(g_log, f, a); va_end(a);
    fputc('\n', g_log); fflush(g_log);
}
static int WriteMem(u32 addr, const void* p, size_t n)
{
    DWORD old;
    if (!VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy((void*)addr, p, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
    return 1;
}
static u32 GetGameBuild(void)
{
    DWORD h; DWORD sz = GetFileVersionInfoSizeA("Game.dll", &h);
    if (!sz) return 0;
    char* buf = (char*)malloc(sz); u32 build = 0;
    if (GetFileVersionInfoA("Game.dll", h, sz, buf)) {
        VS_FIXEDFILEINFO* vi; UINT l;
        if (VerQueryValueA(buf, "\\", (LPVOID*)&vi, &l)) build = vi->dwFileVersionLS & 0xFFFF;
    }
    free(buf);
    return build;
}

typedef void* (__stdcall *Alloc_t)(u32 size, const char* file, u32 line, u32 flags);
typedef u32   (__fastcall *Ctor_t)(u32 self, u32 edx, u32 parent, u32 unit, u32 type);
typedef void  (__fastcall *SetPoint_t)(u32 self, u32 edx, u32 point, u32 rel, u32 relPoint, float x, float y, u32 flag);
typedef void  (__fastcall *SetSize_t)(u32 self, u32 edx, float v);
typedef float*(__fastcall *MaxMana_t)(u32 self, u32 edx, float* out);
typedef float (__fastcall *GetSize_t)(u32 self, u32 edx);
typedef int   (__fastcall *Show_t)(u32 self, u32 edx);
typedef void  (__fastcall *SetColor_t)(u32 self, u32 edx, const u32* argb);
typedef void  (__fastcall *SetUnit_t)(u32 self, u32 edx, u32 unit);
static SetUnit_t orig_SetUnit, orig_SetOther;
typedef u32 (__fastcall *Dtor_t)(u32 self, u32 edx, u32 flags);
static Dtor_t orig_Dtor;

static int IsStatBar(u32 f) { return f && !IsBadReadPtr((void*)f, SB_SIZE) && *(u32*)f == g_base + A->vtStatBar; }
static int IsUnitHpBar(u32 f) { return *(u32*)(f + OFS_TYPE) == TYPE_HP_SMALL && *(u32*)(f + OFS_PARENT) == 0; }

// health bar -> its mana bar
#define MAXBARS 1024
static u32 g_hp[MAXBARS], g_mana[MAXBARS]; static int g_n;
static u32 ManaOf(u32 hp)
{
    for (int i = 0; i < g_n; i++) if (g_hp[i] == hp) {
        u32 m = g_mana[i];
        if (IsStatBar(m) && *(u32*)(m + OFS_PARENT) == hp && *(u32*)(m + OFS_TYPE) == TYPE_MANA_SMALL) return m;
        g_hp[i] = g_hp[--g_n]; g_mana[i] = g_mana[g_n];          // gone (the health bar was freed and reused)
        return 0;
    }
    return 0;
}
static void Hide(u32 f)
{
    *(u32*)(f + OFS_WANT) = 0;
    if (*(u32*)(f + OFS_SHOWN)) g_nHide++;
    if (*(u32*)(f + OFS_SHOWN)) ((Show_t)(*(u32**)f)[0x64 / 4])(f, 0);
}
static void Show(u32 f)
{
    *(u32*)(f + OFS_WANT) = 1;
    if (!*(u32*)(f + OFS_SHOWN)) g_nShow++;
    if (!*(u32*)(f + OFS_SHOWN)) ((Show_t)(*(u32**)f)[0x68 / 4])(f, 0);
}
static float MaxMana(u32 unit)
{
    float v = 0.0f;
    if (IsBadReadPtr((void*)(unit + 0xB8), 4)) return 0.0f;
    ((MaxMana_t)(g_base + A->maxMana))(unit + 0xB8, 0, &v);
    return v;
}
static u32 MakeManaBar(u32 hp, u32 unit)
{
    if (g_n >= MAXBARS) return 0;
    u32 m = (u32)((Alloc_t)(g_base + A->alloc))(SB_SIZE, (const char*)(g_base + A->allocFile), 1, 0);
    if (!m) return 0;
    ((Ctor_t)(g_base + A->ctor))(m, 0, hp, unit, TYPE_MANA_SMALL);
    SetPoint_t sp = (SetPoint_t)(g_base + A->setFramePoint);
    // a bar's black backing reaches 0.001 past its fill on every side: one backing width below the health bar the
    // two fills do not overlap any backing, and a thin black line parts them
    sp(m, 0, 0, hp, 6, 0.0f, -0.001f, 1);                       // TOPLEFT  -> health bar BOTTOMLEFT
    sp(m, 0, 2, hp, 8, 0.0f, -0.001f, 1);                       // TOPRIGHT -> health bar BOTTOMRIGHT
    float h = ((GetSize_t)(*(u32**)hp)[7])(hp, 0);
    if (!(h > 0.0f && h < 0.05f)) h = 0.004f;
    ((SetSize_t)(g_base + A->setHeight))(m, 0, h * g_heightPct / 100.0f);
    u32 tex = *(u32*)(m + 0x134);
    if (tex) ((SetColor_t)(g_base + A->setColor))(tex, 0, &g_color);
    g_nMake++;
    g_hp[g_n] = hp; g_mana[g_n] = m; g_n++;
    static int k; if (k++ < 3) logf_("mana bar %08X made for health bar %08X (level %u, height %.4f)", m, hp, *(u32*)(hp + OFS_LEVEL), h);
    return m;
}
// keep the mana bar on the health bar's frame level (as the game's SetFrameLevel: hide, set, show again)
static void SyncLevel(u32 m, u32 hp)
{
    u32 lv = *(u32*)(hp + OFS_LEVEL);
    if (*(u32*)(m + OFS_LEVEL) == lv) return;
    g_nLevel++;
    int shown = *(u32*)(m + OFS_SHOWN);
    if (shown) ((Show_t)(*(u32**)m)[0x64 / 4])(m, 0);
    *(u32*)(m + OFS_LEVEL) = lv;
    if (shown) ((Show_t)(*(u32**)m)[0x68 / 4])(m, 0);
}
static void __fastcall SetUnit_hook(u32 self, u32 edx, u32 unit)
{
    orig_SetUnit(self, edx, unit);
    if (!IsUnitHpBar(self)) return;
    g_nCall++;
    if (g_log && GetTickCount() - g_diagTick > 3000) {
        g_diagTick = GetTickCount();
        logf_("3 s: %u binds, %u made, %u shown, %u hidden, %u level changes, %d bars", g_nCall, g_nMake, g_nShow, g_nHide, g_nLevel, g_n);
        g_nCall = g_nMake = g_nShow = g_nHide = g_nLevel = 0;
    }
    u32 m = ManaOf(self);
    if (!unit || MaxMana(unit) <= 0.0f) {
        if (m && *(u32*)(m + OFS_SHOWN)) { static int k; if (k++ < 40) logf_("hide: bar %08X unit %08X max %.1f ret %06X", self, unit, unit ? MaxMana(unit) : -1.0f, (u32)__builtin_return_address(0) - g_base); }
        if (m) Hide(m);
        return;
    }
    if (!m && !(m = MakeManaBar(self, unit))) return;
    SyncLevel(m, self);
    Show(m);
    orig_SetUnit(m, 0, unit);
}
// CStatBar +0x78 binds a widget: the game calls it for items and destructables, and also every 0.25 s on every bar
// with the widget it already shows (+0x154) to refresh it. A refresh with the unit of the mana bar keeps it; a bar
// bound to anything else has no mana bar
static void __fastcall SetOther_hook(u32 self, u32 edx, u32 w)
{
    orig_SetOther(self, edx, w);
    if (!IsUnitHpBar(self)) return;
    u32 m = ManaOf(self);
    if (m && w && w == *(u32*)(m + 0x154)) return;
    if (m && *(u32*)(m + OFS_SHOWN)) { static int k; if (k++ < 40) logf_("hide (other): bar %08X widget %08X ret %06X", self, w, (u32)__builtin_return_address(0) - g_base); }
    if (m) Hide(m);
}

// a health bar being destroyed: destroy its mana bar first (it detaches itself from the health bar)
static u32 __fastcall Dtor_hook(u32 self, u32 edx, u32 flags)
{
    for (int i = 0; i < g_n; i++) if (g_hp[i] == self) {
        u32 m = g_mana[i];
        g_hp[i] = g_hp[--g_n]; g_mana[i] = g_mana[g_n];
        if (IsStatBar(m) && *(u32*)(m + OFS_PARENT) == self) orig_Dtor(m, 0, 1);
        break;
    }
    return orig_Dtor(self, edx, flags);
}

static void Install(void)
{
    char dir[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* sl = strrchr(dir, '\\'); if (sl) *(sl + 1) = 0;
    snprintf(path, sizeof path, "%sW3ManaBars.ini", dir);
    g_heightPct = (float)GetPrivateProfileIntA("W3ManaBars", "Height", 100, path);
    if (g_heightPct < 20.0f) g_heightPct = 20.0f;
    if (g_heightPct > 300.0f) g_heightPct = 300.0f;
    { char c[32]; GetPrivateProfileStringA("W3ManaBars", "ManaColor", "", c, sizeof c, path);
      char* e; unsigned long v = strtoul(c[0] == '#' ? c + 1 : c, &e, 16);
      if (c[0] && *e == 0 && e != c) g_color = 0xFF000000u | (v & 0xFFFFFF); }
    g_debug = GetPrivateProfileIntA("W3ManaBars", "Debug", 0, path);
    if (g_debug) { snprintf(path, sizeof path, "%sW3ManaBars.log", dir); g_log = fopen(path, "w"); }

    g_base = (u32)GetModuleHandleA("Game.dll");
    u32 build = GetGameBuild();
    logf_("W3ManaBars 1.0  Game.dll build %u", build);
    for (size_t i = 0; i < sizeof kAddrs / sizeof kAddrs[0]; i++) if (kAddrs[i].build == build) A = &kAddrs[i];
    if (!g_base || !A) { logf_("unsupported game version, doing nothing (need 1.26a, 1.27b or 1.28.5)"); return; }
    u32 vt = g_base + A->vtStatBar;
    if (IsBadReadPtr((void*)vt, 0x80)) { logf_("health bar class not found"); return; }
    orig_SetUnit = (SetUnit_t)*(u32*)(vt + 0x74);
    orig_SetOther = (SetUnit_t)*(u32*)(vt + 0x78);
    orig_Dtor = (Dtor_t)*(u32*)(vt + 0x08);
    u32 h1 = (u32)SetUnit_hook, h2 = (u32)SetOther_hook, h3 = (u32)Dtor_hook;
    WriteMem(vt + 0x08, &h3, 4);
    WriteMem(vt + 0x74, &h1, 4);
    WriteMem(vt + 0x78, &h2, 4);
    logf_("mana bars on (height %.0f%%)", g_heightPct);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        { HMODULE self; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)DllMain, &self); }
        Install();
    }
    return TRUE;
}
