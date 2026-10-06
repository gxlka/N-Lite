int WINAPI wWinMain(HINSTANCE inst,HINSTANCE, PWSTR cmd,int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    gDpi=GetDpiForSystem();if(!gDpi)gDpi=96;
    gExePath.resize(32768);DWORD n=GetModuleFileNameW(nullptr,gExePath.data(),static_cast<DWORD>(gExePath.size()));gExePath.resize(n);
    std::wstring args=cmd?cmd:L"";
    gUserSid=CurrentUserSid();
    gCleanerRoot=ProgramDataNlite();
    gMutex=CreateMutexW(nullptr,TRUE,L"Local\\N-Lite-Single-Instance");
    if(gMutex&&GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(gMutex);return 0;}
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES};InitCommonControlsEx(&ic);
    WNDCLASSEXW pc{};pc.cbSize=sizeof(pc);pc.hInstance=inst;pc.lpfnWndProc=PopupWndProc;pc.lpszClassName=POPUP_CLASS;
    pc.hCursor=LoadCursorW(nullptr,IDC_ARROW);pc.hbrBackground=nullptr;pc.style=CS_DROPSHADOW;
    if(!RegisterClassExW(&pc))return 1;
    gIcon=static_cast<HICON>(LoadImageW(inst,MAKEINTRESOURCEW(IDI_NLITE),IMAGE_ICON,32,32,LR_DEFAULTCOLOR));
    if(!gIcon)gIcon=MakeIcon();
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=inst;wc.lpfnWndProc=WndProc;wc.lpszClassName=APP_CLASS;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=gIcon;wc.hIconSm=gIcon;
    wc.hbrBackground=nullptr;wc.style=CS_DBLCLKS;
    if(!RegisterClassExW(&wc))return 1;
    HWND h=CreateWindowExW(0,APP_CLASS,L"N Lite",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,
        UiLogicalToDevice(1240,gDpi),UiLogicalToDevice(830,gDpi),nullptr,nullptr,inst,nullptr);
    if(!h)return 1;
    AddTray();
    if(args.find(L"--startup")!=std::wstring::npos)ShowWindow(h,SW_HIDE);
    else {ShowWindow(h,show);UpdateWindow(h);}
    MSG m;while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
    if(gMutex)CloseHandle(gMutex);
    return static_cast<int>(m.wParam);
}
