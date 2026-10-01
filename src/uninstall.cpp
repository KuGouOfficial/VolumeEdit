#include "audio.h"
#include "portable.h"
#include "gui_snapshot.h"
#include "icons.h"
#include "product_version.h"
#include "permission_helper.h"
#include <commctrl.h>
#include <shellapi.h>
#include <thread>
#include <string>
#include <cwchar>
#include <fstream>

namespace {
HWND window,status_line,button;HFONT font;HINSTANCE instance;
std::thread worker;bool preview=false,cleanup_smoke=false;
constexpr UINT Done=WM_APP+1;
struct Result{bool success;std::wstring text;};
std::filesystem::path root;
void close_client(){
    struct Search{std::wstring image;std::vector<DWORD> pids;};Search search{(root/L"VolumeEdit.exe").wstring(),{}};
    EnumWindows([](HWND h,LPARAM value)->BOOL{
        auto& s=*reinterpret_cast<Search*>(value);wchar_t name[128];GetClassNameW(h,name,128);if(wcscmp(name,L"VolumeEdit.Main")!=0)return TRUE;
        DWORD pid=0;GetWindowThreadProcessId(h,&pid);ve::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
        if(!process)return TRUE;
        wchar_t image[32768];DWORD size=32768;if(!QueryFullProcessImageNameW(process.get(),0,image,&size))return TRUE;
        if(_wcsicmp(image,s.image.c_str())==0){s.pids.push_back(pid);PostMessageW(h,WM_COMMAND,4003,0);}return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    for(const auto pid:search.pids){ve::Handle process(OpenProcess(SYNCHRONIZE,FALSE,pid));if(process&&WaitForSingleObject(process.get(),15000)!=WAIT_OBJECT_0)throw std::runtime_error("客户端尚未退出。请关闭音量恢复提示后重试。");}
}
void perform(){
    auto* result=new Result{false,L""};
    try{
        ve::Apartment apartment;ve::verify_product(root);
        // Holding the singleton prevents relaunch while product files are removed.
        ve::Handle singleton(CreateMutexW(nullptr,FALSE,L"Local\\VolumeEdit.Portable.v3"));if(!singleton)ve::win(FALSE);
        if(!cleanup_smoke)ve::ensure_helper_running();
        close_client();
        if(ve::restore_saved_volumes()!=0)throw std::runtime_error("部分原音量尚未恢复。请连接原输出设备并重新打开相关应用，再点击卸载。恢复记录和程序文件已保留。");
        if(!cleanup_smoke)ve::remove_helper(window);
        ve::clear_startup();
        wchar_t windows[32768];const auto n=GetWindowsDirectoryW(windows,32768);if(!n||n>=32768)ve::win(FALSE);ve::win(SetCurrentDirectoryW(windows));
        ve::remove_product_files(root,true);ve::launch_final_cleanup(root,!cleanup_smoke);
        result->success=true;result->text=L"正在完成自身文件清理…";
    }catch(const ve::Failure& f){result->text=L"卸载未完成："+ve::error_text(static_cast<DWORD>(f.code))+L"\n请关闭占用文件的程序，然后重试。";}
    catch(const std::exception& e){result->text=L"卸载未完成："+ve::wide(e.what());}
    if(!PostMessageW(window,Done,0,reinterpret_cast<LPARAM>(result)))delete result;
}
LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM w,LPARAM l){
    if(msg==WM_CREATE){
        window=h;const UINT dpi=GetDpiForWindow(h);ve::window_icons(h,instance,dpi);font=CreateFontW(-MulDiv(16,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        auto create=[&](const wchar_t* cls,const wchar_t* text,int id,int x,int y,int width,int height){auto child=CreateWindowExW(0,cls,text,WS_VISIBLE|WS_CHILD|WS_TABSTOP,MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(width,dpi,96),MulDiv(height,dpi,96),h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return child;};
        create(L"STATIC",(std::wstring(L"卸载 VolumeEdit ")+ve::DisplayVersion).c_str(),0,22,20,515,30);
        create(L"STATIC",L"将关闭客户端并恢复音量；若启用了权限辅助，\n将请求管理员确认并清除服务及其受保护文件。\n随后清理启动项和产品文件，其他文件会保留。",0,22,64,515,98);
        status_line=create(L"STATIC",L"准备就绪。",0,22,177,515,100);
        button=create(L"BUTTON",L"卸载",100,22,300,200,38);create(L"BUTTON",L"关闭",101,330,300,200,38);return 0;
    }
    if(msg==WM_COMMAND){if(LOWORD(w)==100&&!preview){if(worker.joinable())worker.join();EnableWindow(button,FALSE);EnableWindow(GetDlgItem(h,101),FALSE);SetWindowTextW(status_line,L"正在恢复音量并清理…");worker=std::thread(perform);}else if(LOWORD(w)==101)SendMessageW(h,WM_CLOSE,0,0);return 0;}
    if(msg==Done){auto* result=reinterpret_cast<Result*>(l);if(worker.joinable())worker.join();SetWindowTextW(status_line,result->text.c_str());EnableWindow(button,!result->success);EnableWindow(GetDlgItem(h,101),TRUE);const bool success=result->success;delete result;if(success)DestroyWindow(h);return 0;}
    if(msg==WM_CLOSE){if(worker.joinable())return 0;DestroyWindow(h);return 0;}
    if(msg==WM_DESTROY){DeleteObject(font);PostQuitMessage(0);return 0;}
    if(msg==WM_CTLCOLORSTATIC){SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));}
    return DefWindowProcW(h,msg,w,l);
}
}
int WINAPI wWinMain(HINSTANCE app,HINSTANCE,LPWSTR args,int){
    int count=0;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);if(!arguments)return 1;
    std::wstring argument=count==2?arguments[1]:L"";LocalFree(arguments);if(count>2)return 1;args=argument.data();
    try{
        instance=app;root=ve::executable_directory();
        if(wcscmp(args,L"--self-delete-smoke")==0){
            // This developer test only accepts a specifically marked disposable copy.
            if(!root.filename().wstring().starts_with(L"self-delete-"))return 10;
            std::ifstream marker(root/L"product.id");std::string line;std::getline(marker,line);if(line!=ve::ProductId)return 11;
            marker.close();
            if(std::distance(std::filesystem::directory_iterator(root),std::filesystem::directory_iterator{})!=2)return 12;
            wchar_t windows[32768];GetWindowsDirectoryW(windows,32768);ve::win(SetCurrentDirectoryW(windows));
            ve::launch_final_cleanup(root,false);return 0;
        }
        ve::Apartment apartment;preview=wcscmp(args,L"--smoke")==0||wcscmp(args,L"--snapshot")==0;if(!preview)ve::verify_product(root);
        cleanup_smoke=wcscmp(args,L"--uninstall-smoke")==0;
        if(cleanup_smoke&&(!root.filename().wstring().starts_with(L"uninstall-test-")||std::filesystem::exists(root/L"state")))return 15;
        WNDCLASSW klass{};klass.hInstance=instance;klass.lpszClassName=L"VolumeEdit.Uninstall";klass.lpfnWndProc=procedure;klass.hIcon=ve::product_icon(instance,GetDpiForSystem(),false);klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&klass);
        const auto dpi=GetDpiForSystem();auto h=CreateWindowExW(0,klass.lpszClassName,L"VolumeEdit 卸载",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(580,dpi,96),MulDiv(405,dpi,96),nullptr,nullptr,instance,nullptr);if(!h)return 1;SetWindowTextW(h,(std::wstring(L"VolumeEdit ")+ve::DisplayVersion+L" 卸载").c_str());
        if(wcscmp(args,L"--smoke")==0){const bool valid=ve::product_icon(instance,96,true)!=nullptr&&ve::product_icon(instance,96,false)!=nullptr&&IsWindow(button)&&IsWindow(status_line);DestroyWindow(h);return valid?0:2;}
        if(!cleanup_smoke){ShowWindow(h,SW_SHOW);UpdateWindow(h);}else SendMessageW(h,WM_COMMAND,100,0);
        if(preview){snapshot(h,root/L"reports"/L"uninstall-preview.png");DestroyWindow(h);return 0;}
        MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(h,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}return 0;
    }catch(const std::exception& e){if(wcscmp(args,L"--self-delete-smoke")==0)return 14;MessageBoxW(nullptr,ve::wide(e.what()).c_str(),L"VolumeEdit 卸载未完成",MB_OK|MB_ICONERROR);return 1;}
}
