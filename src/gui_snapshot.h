#pragma once
#include "common.h"
#include <wincodec.h>

// Development-only capture of this application's own window for layout review.
inline void snapshot(HWND window,const std::filesystem::path& path){
    std::filesystem::create_directories(path.parent_path());
    RECT bounds{};GetWindowRect(window,&bounds);const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    HDC screen=GetDC(window),memory=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);const auto previous=SelectObject(memory,bitmap);
    struct Release{HWND h;HDC screen,memory;HBITMAP bitmap;HGDIOBJ previous;~Release(){SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(h,screen);}} release{window,screen,memory,bitmap,previous};
    FillRect(memory,&bounds,GetSysColorBrush(COLOR_WINDOW));
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    ve::Com<IWICImagingFactory> factory;ve::hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ve::Com<IWICBitmap> pixels;ve::hr(factory->CreateBitmapFromHBITMAP(bitmap,nullptr,WICBitmapIgnoreAlpha,&pixels));
    ve::Com<IWICStream> stream;ve::hr(factory->CreateStream(&stream));ve::hr(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
    ve::Com<IWICBitmapEncoder> encoder;ve::hr(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));ve::hr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ve::Com<IWICBitmapFrameEncode> frame;ve::hr(encoder->CreateNewFrame(&frame,nullptr));ve::hr(frame->Initialize(nullptr));ve::hr(frame->SetSize(width,height));
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;ve::hr(frame->SetPixelFormat(&format));ve::hr(frame->WriteSource(pixels.Get(),nullptr));ve::hr(frame->Commit());ve::hr(encoder->Commit());
}
