#ifndef _SWELL_H_
#define _SWELL_H_

#include <cstdint>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

// Windows type definitions for Reaper SDK cross-platform compatibility
typedef void* HWND;
typedef void* HMENU;
typedef void* HINSTANCE;
typedef void* HGDIOBJ;
typedef void* HDC;
typedef void* HFONT;
typedef void* HBRUSH;
typedef void* HPEN;
typedef void* HBITMAP;
typedef void* HICON;
typedef void* HCURSOR;

// Windows integer types
typedef int BOOL;
typedef unsigned int UINT;
typedef long LONG;
typedef unsigned long ULONG;
typedef int32_t INT;
typedef uint32_t DWORD;
typedef int64_t INT_PTR;
typedef uint64_t UINT_PTR;
typedef int32_t LRESULT;
typedef int32_t WPARAM;
typedef int64_t LPARAM;

// WDL types
typedef int64_t WDL_INT64;

// GUID structure for cross-platform compatibility
typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;

// RECT structure
typedef struct _RECT {
    int left;
    int top;
    int right;
    int bottom;
} RECT;

// Windows message structure
typedef struct _MSG {
    HWND hwnd;
    unsigned int message;
    int wParam;
    int lParam;
    uint32_t time;
    int pt_x;
    int pt_y;
} MSG;

// Accelerator structure
typedef struct _ACCEL {
    uint8_t fVirt;
    uint16_t key;
    uint16_t cmd;
} ACCEL;

// Common Windows constants
#define FALSE 0
#define TRUE 1
#define NULL 0

#endif
