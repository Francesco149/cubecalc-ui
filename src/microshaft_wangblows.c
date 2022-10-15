#ifndef MICROSHAFT_WANGBLOWS_H
#define MICROSHAFT_WANGBLOWS_H

#if defined(_WIN32) || defined(_WIN64) || defined(__MINGW32__)
#define MICROSHAFT_WANGBLOWS
#endif

#ifdef MICROSHAFT_WANGBLOWS
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <windows.h>
#include <Shlobj.h>
#include <shlobj_core.h>
#endif

#endif
#if defined(MICROSHAFT_WANGBLOWS_IMPLEMENTATION) && !defined(MICROSHAFT_WANGBLOWS_UNIT)

#endif
