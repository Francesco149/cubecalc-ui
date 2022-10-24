#ifndef MICROSHAFT_WANGBLOWS_H
#define MICROSHAFT_WANGBLOWS_H

#if defined(_WIN32) || defined(_WIN64) || defined(__MINGW32__)
#define MICROSHAFT_WANGBLOWS
#endif

#ifdef MICROSHAFT_WANGBLOWS
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>
#include <shlobj.h>
#include <timeapi.h>
#include <dwmapi.h>
#endif

#endif
#if defined(MICROSHAFT_WANGBLOWS_IMPLEMENTATION) && !defined(MICROSHAFT_WANGBLOWS_UNIT)

#endif
