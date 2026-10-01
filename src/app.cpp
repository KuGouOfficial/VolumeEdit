#include "audio.h"
#include "portable.h"
#include "gui_snapshot.h"
#include "icons.h"
#include "product_version.h"
#include <commctrl.h>
#include <shellapi.h>
#include <memory>
#include <cmath>
#include <cwchar>
#include <algorithm>

namespace {
enum {Gain=100,Target,Refresh,Startup,Zero,Numeric};
constexpr UINT TrayMessage=WM_APP+1;
HWND window,slider,label,numeric,target,status_line,summary;
HFONT font{};HINSTANCE instance;UINT dpi=96;
std::vector<ve::Device> list;
std::unique_ptr<ve::AudioEngine> engine;
bool preview=false;
HWND control(const wchar_t* klass,const wchar_t* text,int id,int x,int y,int w,int h,DWORD style=0){
    auto c=CreateWindowExW(0,klass,text,WS_CHILD|WS_VISIBLE|style,MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(h,dpi,96),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;
}
void report(const std::exception& error){const auto* f=dynamic_cast<const ve::Failure*>(&error);MessageBoxW(window,f?ve::error_text(static_cast<DWORD>(f->code)).c_str():ve::wide(error.what()).c_str(),L"VolumeEdit 操作未完成",MB_OK|MB_ICONERROR);}
void gain_label(){
    const int value=static_cast<int>(SendMessageW(slider,TBM_GETPOS,0,0))-400;
    wchar_t text[100];if(value==0)wcscpy_s(text,L"0.0 dB  ·  基准音量");else swprintf_s(text,L"%+.1f dB",value/10.0);
    SetWindowTextW(label,text);swprintf_s(text,L"%.1f",value/10.0);SetWindowTextW(numeric,text);
    InvalidateRect(label,nullptr,TRUE);InvalidateRect(GetDlgItem(window,Zero),nullptr,TRUE);
}
std::wstring selected(){const auto row=SendMessageW(target,CB_GETCURSEL,0,0);if(row<=0)return {};const auto index=static_cast<std::size_t>(SendMessageW(target,CB_GETITEMDATA,row,0));return index<list.size()?list[index].id:L"";}
void refresh(){
    const auto remembered=engine?engine->settings():ve::Settings{};
    SendMessageW(target,CB_RESETCONTENT,0,0);
    std::wstring caption=L"自动 · Windows 当前默认输出";
    try{list=ve::devices();const auto id=ve::default_output_id();for(const auto& d:list)if(d.id==id)caption=L"自动 · "+d.name;}catch(...){list.clear();}
    SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(caption.c_str()));SendMessageW(target,CB_SETCURSEL,0,0);
    for(std::size_t i=0;i<list.size();++i){const auto row=SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(list[i].name.c_str()));SendMessageW(target,CB_SETITEMDATA,row,i);if(!remembered.automatic_target&&remembered.target==list[i].id)SendMessageW(target,CB_SETCURSEL,row,0);}
    if(!remembered.automatic_target&&SendMessageW(target,CB_GETCURSEL,0,0)==0){list.push_back({remembered.target,L"已断开 · 保存的输出设备"});const auto row=SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(list.back().name.c_str()));SendMessageW(target,CB_SETITEMDATA,row,list.size()-1);SendMessageW(target,CB_SETCURSEL,row,0);}
}
void apply(){
    if(!engine)return;
    try{auto settings=engine->settings();settings.gain_db_x10=static_cast<int>(SendMessageW(slider,TBM_GETPOS,0,0))-400;settings.automatic_target=SendMessageW(target,CB_GETCURSEL,0,0)==0;settings.target=selected();if(!engine->configure(settings))throw std::runtime_error("输出设备已断开，请刷新设备。");}
    catch(const std::exception& e){report(e);}
}
void numeric_apply(){
    wchar_t text[64];GetWindowTextW(numeric,text,64);wchar_t* end=nullptr;const double value=std::wcstod(text,&end);
    if(end==text||*end||!std::isfinite(value)||value < -40 || value > 40){gain_label();return;}
    SendMessageW(slider,TBM_SETPOS,TRUE,static_cast<int>(std::lround(value*10))+400);gain_label();apply();
}
LRESULT CALLBACK numeric_proc(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){if(msg==WM_KEYDOWN&&w==VK_RETURN){numeric_apply();return 0;}return DefSubclassProc(h,msg,w,l);}
void tray(bool add){if(preview)return;NOTIFYICONDATAW n{sizeof(n)};n.hWnd=window;n.uID=1;n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n.uCallbackMessage=TrayMessage;n.hIcon=ve::product_icon(instance,dpi,true);wcscpy_s(n.szTip,L"VolumeEdit 会话音量微调");Shell_NotifyIconW(add?NIM_ADD:NIM_DELETE,&n);}
void show(){ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}
void status(){
    if(!engine)return;const auto s=engine->status();std::wstring text;
    switch(s.state){case ve::AudioState::Running:text=L"正在调节当前用户的应用会话音量";break;case ve::AudioState::Waiting:text=L"等待输出设备或有效配置";break;case ve::AudioState::Error:text=L"音量调节未完成";break;default:text=L"正在启动";}
    if(FAILED(s.error))text+=L"\n"+ve::error_text(static_cast<DWORD>(s.error));
    if(s.capped_sessions)text+=L"\n"+std::to_wstring(s.capped_sessions)+L" 个会话已达到 100% 上限，无法继续提高。";
    if(!s.saved)text+=L"\n配置未保存；请确认解压目录可写。";
    SetWindowTextW(status_line,text.c_str());
    text=L"可访问会话："+std::to_wstring(s.sessions)+L"    原音量记录："+std::to_wstring(s.pending_restore);SetWindowTextW(summary,text.c_str());
}
LRESULT CALLBACK procedure(HWND h,UINT message,WPARAM wp,LPARAM lp){
    if(message==WM_CREATE){
        window=h;dpi=GetDpiForWindow(h);ve::window_icons(h,instance,dpi);font=CreateFontW(-MulDiv(16,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        control(L"STATIC",(std::wstring(L"VolumeEdit ")+ve::DisplayVersion+L"  ·  耳机音量微调").c_str(),0,22,18,550,30);
        control(L"STATIC",L"输出设备",0,22,65,530,22);
        target=control(WC_COMBOBOXW,L"",Target,22,93,550,180,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        control(L"STATIC",L"会话音量调节（−40～+40 dB）",0,22,137,530,22);
        label=control(L"STATIC",L"",0,22,172,350,30);
        numeric=control(L"EDIT",L"0.0",Numeric,440,170,130,28,ES_AUTOHSCROLL|WS_BORDER|WS_TABSTOP);SetWindowSubclass(numeric,numeric_proc,1,0);
        slider=control(TRACKBAR_CLASSW,L"",Gain,16,212,560,42,TBS_AUTOTICKS|WS_TABSTOP);
        SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELPARAM(0,800));SendMessageW(slider,TBM_SETPOS,TRUE,400);SendMessageW(slider,TBM_SETTICFREQ,100,0);
        control(L"STATIC",L"−40 dB",0,22,256,110,22);control(L"STATIC",L"● 0 dB 基准",0,240,256,135,22);control(L"STATIC",L"+40 dB",0,495,256,80,22);
        control(L"BUTTON",L"音量复位",Zero,22,299,175,36,BS_OWNERDRAW|WS_TABSTOP);
        control(L"BUTTON",L"刷新设备",Refresh,214,299,155,36,WS_TABSTOP);
        control(L"BUTTON",L"开机启动",Startup,390,299,180,36,BS_AUTOCHECKBOX|WS_TABSTOP);
        status_line=control(L"STATIC",L"正在启动…",0,22,355,550,70);
        summary=control(L"STATIC",L"",0,22,430,550,24);
        control(L"STATIC",L"正 dB 仅提高会话音量，最高 100%；退出恢复原音量。",0,22,473,550,24);
        gain_label();refresh();tray(true);return 0;
    }
    if(message==WM_HSCROLL){gain_label();SetTimer(window,1,100,nullptr);return 0;}
    if(message==WM_TIMER){if(wp==1){KillTimer(window,1);apply();}else if(wp==2)status();else if(wp==3&&!SendMessageW(target,CB_GETDROPPEDSTATE,0,0))refresh();return 0;}
    if(message==WM_COMMAND){
        if(LOWORD(wp)==Target&&HIWORD(wp)==CBN_SELCHANGE){apply();return 0;}
        if(LOWORD(wp)==Numeric&&HIWORD(wp)==EN_KILLFOCUS){numeric_apply();return 0;}
        switch(LOWORD(wp)){
        case Zero:KillTimer(window,1);SendMessageW(slider,TBM_SETPOS,TRUE,400);gain_label();apply();break;
        case Refresh:refresh();break;
        case Startup:if(!preview)try{ve::set_startup(SendMessageW(GetDlgItem(h,Startup),BM_GETCHECK,0,0)==BST_CHECKED);}catch(const std::exception& e){report(e);try{SendMessageW(GetDlgItem(h,Startup),BM_SETCHECK,ve::startup_enabled()?BST_CHECKED:BST_UNCHECKED,0);}catch(...){}}break;
        case 4001:show();break;
        case 4003:DestroyWindow(window);break;
        }return 0;
    }
    if(message==TrayMessage){if(lp==WM_LBUTTONUP||lp==WM_LBUTTONDBLCLK)show();else if(lp==WM_RBUTTONUP){auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,4001,L"打开");AppendMenuW(menu,MF_STRING,4003,L"退出并恢复音量");POINT p;GetCursorPos(&p);SetForegroundWindow(window);TrackPopupMenu(menu,TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);DestroyMenu(menu);}return 0;}
    if(message==WM_CLOSE){ShowWindow(window,SW_HIDE);return 0;}
    if(message==WM_CTLCOLORSTATIC){const auto dc=reinterpret_cast<HDC>(wp);SetBkMode(dc,TRANSPARENT);const bool zero=SendMessageW(slider,TBM_GETPOS,0,0)==400;SetTextColor(dc,reinterpret_cast<HWND>(lp)==label&&zero?RGB(0,94,170):GetSysColor(COLOR_WINDOWTEXT));return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));}
    if(message==WM_DRAWITEM&&wp==Zero){
        const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);const bool zero=SendMessageW(slider,TBM_GETPOS,0,0)==400;
        auto brush=CreateSolidBrush(zero?RGB(224,241,255):RGB(255,255,255));FillRect(item->hDC,&item->rcItem,brush);DeleteObject(brush);
        FrameRect(item->hDC,&item->rcItem,GetSysColorBrush(COLOR_HIGHLIGHT));const auto old=SelectObject(item->hDC,font);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,RGB(0,94,170));RECT rect=item->rcItem;DrawTextW(item->hDC,L"音量复位",-1,&rect,DT_SINGLELINE|DT_CENTER|DT_VCENTER);SelectObject(item->hDC,old);if(item->itemState&ODS_FOCUS){InflateRect(&rect,-4,-4);DrawFocusRect(item->hDC,&rect);}return TRUE;
    }
    if(message==WM_NOTIFY&&reinterpret_cast<NMHDR*>(lp)->hwndFrom==slider&&reinterpret_cast<NMHDR*>(lp)->code==NM_CUSTOMDRAW){
        const auto* draw=reinterpret_cast<NMCUSTOMDRAW*>(lp);if(draw->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYPOSTPAINT;
        if(draw->dwDrawStage==CDDS_POSTPAINT){RECT channel;SendMessageW(slider,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));auto pen=CreatePen(PS_SOLID,MulDiv(3,dpi,96),RGB(0,94,170));const auto old=SelectObject(draw->hdc,pen);MoveToEx(draw->hdc,(channel.left+channel.right)/2,channel.bottom+2,nullptr);LineTo(draw->hdc,(channel.left+channel.right)/2,channel.bottom+MulDiv(10,dpi,96));SelectObject(draw->hdc,old);DeleteObject(pen);}return CDRF_DODEFAULT;
    }
    if(message==WM_DPICHANGED){
        const UINT next=HIWORD(wp),previous=dpi;dpi=next;ve::window_icons(h,instance,dpi);tray(true);const auto replacement=CreateFontW(-MulDiv(16,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){RECT rect;GetWindowRect(child,&rect);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);SetWindowPos(child,nullptr,MulDiv(rect.left,next,previous),MulDiv(rect.top,next,previous),MulDiv(rect.right-rect.left,next,previous),MulDiv(rect.bottom-rect.top,next,previous),SWP_NOZORDER|SWP_NOACTIVATE);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(replacement),TRUE);}DeleteObject(font);font=replacement;const auto* r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;
    }
    if(message==WM_DESTROY){
        KillTimer(window,1);KillTimer(window,2);KillTimer(window,3);
        if(engine){engine->stop();const auto s=engine->status();if(s.pending_restore||FAILED(s.error))MessageBoxW(h,L"部分会话原音量尚未恢复。已保留 state 中的记录；请重新连接输出设备并打开相关应用，再运行 VolumeEdit 或 uninstall.exe 重试。",L"VolumeEdit 恢复未完成",MB_OK|MB_ICONWARNING);engine.reset();}
        tray(false);DeleteObject(font);PostQuitMessage(0);return 0;
    }
    if(message==RegisterWindowMessageW(L"TaskbarCreated")){tray(true);return 0;}
    return DefWindowProcW(h,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE app,HINSTANCE,LPWSTR args,int){
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);if(!arguments)return 1;
    std::wstring argument=count==2?arguments[1]:L"";LocalFree(arguments);if(count>2)return 1;args=argument.data();
    try{
        ve::Apartment apartment;instance=app;preview=wcscmp(args,L"--smoke")==0||wcscmp(args,L"--snapshot")==0;
        ve::Handle singleton;if(!preview){singleton.reset(CreateMutexW(nullptr,FALSE,L"Local\\VolumeEdit.Portable.v3"));if(!singleton)ve::win(FALSE);if(GetLastError()==ERROR_ALREADY_EXISTS){auto other=FindWindowW(L"VolumeEdit.Main",nullptr);if(other){ShowWindow(other,SW_SHOW);SetForegroundWindow(other);}return 0;}ve::ensure_state();}
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES};InitCommonControlsEx(&common);
        WNDCLASSW klass{};klass.hInstance=instance;klass.lpszClassName=L"VolumeEdit.Main";klass.lpfnWndProc=procedure;klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);klass.hIcon=ve::product_icon(instance,GetDpiForSystem(),false);RegisterClassW(&klass);
        const auto system_dpi=GetDpiForSystem();auto h=CreateWindowExW(0,klass.lpszClassName,L"VolumeEdit",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(615,system_dpi,96),MulDiv(550,system_dpi,96),nullptr,nullptr,instance,nullptr);if(!h)return 1;SetWindowTextW(h,(std::wstring(L"VolumeEdit ")+ve::DisplayVersion).c_str());
        if(wcscmp(args,L"--smoke")==0){
            bool valid=ve::product_icon(instance,96,true)!=nullptr&&ve::product_icon(instance,96,false)!=nullptr&&IsWindow(slider)&&SendMessageW(slider,TBM_GETPOS,0,0)==400&&SendMessageW(target,CB_GETCURSEL,0,0)==0&&SendMessageW(target,CB_GETLBTEXTLEN,0,0)>0;
            SetWindowTextW(numeric,L"40.0");numeric_apply();valid=valid&&SendMessageW(slider,TBM_GETPOS,0,0)==800;
            SetWindowTextW(numeric,L"40.1");numeric_apply();valid=valid&&SendMessageW(slider,TBM_GETPOS,0,0)==800;
            SetWindowTextW(numeric,L"-40.0");numeric_apply();valid=valid&&SendMessageW(slider,TBM_GETPOS,0,0)==0;
            SetWindowTextW(numeric,L"-40.1");numeric_apply();valid=valid&&SendMessageW(slider,TBM_GETPOS,0,0)==0;
            SendMessageW(h,WM_COMMAND,Zero,0);wchar_t value[32];GetWindowTextW(numeric,value,32);valid=valid&&SendMessageW(slider,TBM_GETPOS,0,0)==400&&wcscmp(value,L"0.0")==0;
            DestroyWindow(h);return valid?0:2;
        }
        if(preview){SetWindowTextW(status_line,L"解压即用 · 当前为界面预览\n默认 0 dB，点击“音量复位”返回基准。");SetWindowTextW(summary,L"可访问会话：0    原音量记录：0");ShowWindow(h,SW_SHOW);UpdateWindow(h);snapshot(h,ve::executable_directory()/L"client-preview.png");DestroyWindow(h);return 0;}
        engine=std::make_unique<ve::AudioEngine>();const auto settings=engine->settings();SendMessageW(slider,TBM_SETPOS,TRUE,settings.gain_db_x10+400);gain_label();refresh();
        SendMessageW(GetDlgItem(h,Startup),BM_SETCHECK,ve::startup_enabled()?BST_CHECKED:BST_UNCHECKED,0);engine->start();SetTimer(h,2,500,nullptr);SetTimer(h,3,3000,nullptr);
        if(wcscmp(args,L"--tray")!=0)ShowWindow(h,SW_SHOW);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}return 0;
    }catch(const std::exception& e){report(e);if(IsWindow(window))DestroyWindow(window);return 1;}
}
