// Provides timeBeginPeriod / timeEndPeriod via dynamic winmm.dll loading.
// This lets us skip linking winmm.lib, which exports a conflicting "PlaySound"
// symbol (WINMM's multimedia API) that clashes with raylib's PlaySound(Sound).
// The timer functions are still fully functional -- they delegate to winmm.dll
// which is always present on Windows.
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" unsigned int __stdcall timeBeginPeriod(unsigned int uPeriod)
{
    static HMODULE hWinMM = LoadLibraryA("winmm.dll");
    typedef unsigned int(__stdcall* Fn)(unsigned int);
    static Fn fn = hWinMM ? reinterpret_cast<Fn>(GetProcAddress(hWinMM, "timeBeginPeriod")) : nullptr;
    return fn ? fn(uPeriod) : 0u;
}

extern "C" unsigned int __stdcall timeEndPeriod(unsigned int uPeriod)
{
    static HMODULE hWinMM = LoadLibraryA("winmm.dll");
    typedef unsigned int(__stdcall* Fn)(unsigned int);
    static Fn fn = hWinMM ? reinterpret_cast<Fn>(GetProcAddress(hWinMM, "timeEndPeriod")) : nullptr;
    return fn ? fn(uPeriod) : 0u;
}

#endif
