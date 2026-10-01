#pragma once
#include "common.h"
namespace ve {
inline constexpr int IconId=101;
inline HICON product_icon(HINSTANCE instance,UINT dpi,bool compact){
    return reinterpret_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IconId),IMAGE_ICON,
        GetSystemMetricsForDpi(compact?SM_CXSMICON:SM_CXICON,dpi),
        GetSystemMetricsForDpi(compact?SM_CYSMICON:SM_CYICON,dpi),LR_SHARED));
}
inline void window_icons(HWND window,HINSTANCE instance,UINT dpi){
    SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(product_icon(instance,dpi,false)));
    SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(product_icon(instance,dpi,true)));
}
}
