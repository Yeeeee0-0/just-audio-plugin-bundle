// Included after Session in the native host. A separate opt-in regression:
// shows only its own test HWND without activation; reads only the combo client.
// A test-only native text colour makes the lost grayscale path reproducible
// even on runners where ClearType is disabled. No system settings are changed.
class NativeTextColourProbe {
    HWND parent, combo;
    static LRESULT CALLBACK observe(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto* self=reinterpret_cast<NativeTextColourProbe*>(data);
        if((m==WM_CTLCOLORSTATIC || m==WM_CTLCOLORBTN || m==WM_CTLCOLOREDIT || m==WM_CTLCOLORLISTBOX)
            && (reinterpret_cast<HWND>(l)==self->combo || IsChild(self->combo,reinterpret_cast<HWND>(l)))) {
            ++self->requests;
            if(GetObjectType(reinterpret_cast<HDC>(w))==OBJ_DC)++self->directScreenRequests;
            SetTextColor(reinterpret_cast<HDC>(w),RGB(230,30,110));
            SetBkColor(reinterpret_cast<HDC>(w),RGB(255,255,255));
            return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
        }
        return DefSubclassProc(h,m,w,l);
    }
public:
    unsigned requests=0,directScreenRequests=0;
    explicit NativeTextColourProbe(HWND value):parent(GetParent(value)),combo(value) {
        check(SetWindowSubclass(parent,observe,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this))!=FALSE,
            "install test-only native text colour probe");
    }
    ~NativeTextColourProbe(){if(IsWindow(parent))RemoveWindowSubclass(parent,observe,reinterpret_cast<UINT_PTR>(this));}
};

unsigned liveComboColourPixels(HWND combo,const fs::path& path) {
    RECT bounds{};check(GetClientRect(combo,&bounds)!=FALSE,"read live combo bounds");
    const int width=bounds.right,height=bounds.bottom;
    check(width>0 && height>0 && width<1024 && height<256,"bounded live combo client");
    HDC source=GetDC(combo),memory=CreateCompatibleDC(source);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(source,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!source || !memory || !bitmap || !pixels){
        if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);if(source)ReleaseDC(combo,source);
        throw std::runtime_error("cannot allocate live combo capture");
    }
    auto previous=SelectObject(memory,bitmap);GdiFlush();
    // Do not call WM_PRINT/WM_PRINTCLIENT or invalidate before this read: that
    // would fix the old pixels during capture and hide the direct-paint bug.
    const BOOL copied=BitBlt(memory,0,0,width,height,source,0,0,SRCCOPY);GdiFlush();
    unsigned coloured=0;auto* bytes=static_cast<const unsigned char*>(pixels);
    for(int i=0;i<width*height;++i){auto* rgb=bytes+i*4;if(std::max({rgb[0],rgb[1],rgb[2]})!=std::min({rgb[0],rgb[1],rgb[2]}))++coloured;}
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+DWORD(width*height*4);
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof(file));
    out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));out.write(reinterpret_cast<const char*>(pixels),width*height*4);
    const bool wrote=bool(out);SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(combo,source);
    check(copied && wrote,"copy existing native client pixels and save evidence");
    log("LIVE_COMBO "+path.filename().u8string()+" non_gray_pixels="+std::to_string(coloured));return coloured;
}

void checkNativeComboPaint(Session& s,const fs::path& evidence) {
    nativeComboLiveProbe=true;
    for(unsigned round=0;round<3;++round){
        s.open();if(!uiState(s.controller).advanced)click(s.window.handle,102);
        if(s.controller->getParamNormalized(0)!=0)click(s.window.handle,101);
        // Only this isolated test-process HWND is shown. No focus request,
        // desktop capture, audio device, REAPER or other application access.
        SetWindowPos(s.window.handle,HWND_TOPMOST,0,0,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        HWND combo=nullptr;EnumChildWindows(s.window.handle,[](HWND h,LPARAM data)->BOOL{
            wchar_t klass[32]{};GetClassNameW(h,klass,32);
            if(!lstrcmpiW(klass,L"COMBOBOX") && GetDlgCtrlID(h)==101){*reinterpret_cast<HWND*>(data)=h;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&combo));
        check(combo && IsWindowVisible(combo),"visible isolated Limiter Algorithm combo");
        {
            WindowLifetime lifetime(combo);NativeTextColourProbe probe(combo);
            const auto selected=SendMessageW(combo,CB_GETCURSEL,0,0);check(selected==0 || selected==1,"two algorithm choices");
            RedrawWindow(s.window.handle,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
            SendMessageW(combo,CB_SETCURSEL,selected,0);
            check(probe.requests>0 && liveComboColourPixels(combo,evidence/("active-"+std::to_string(round)+".bmp"))>0,
                "active native text-colour positive control is visible");
            ++nativeComboActiveControls;
            click(s.window.handle,101);check(s.controller->getParamNormalized(0)==1,"bypass is active for native paint probe");
            const auto writes=s.handler->writes;const auto algorithm=s.controller->getParamNormalized(10);
            unsigned index=0;
            for(auto item:{selected,1-selected,selected,selected}){
                const auto requests=probe.requests;
                const auto direct=probe.directScreenRequests;
                const bool changed=SendMessageW(combo,CB_GETCURSEL,0,0)!=item;
                check(SendMessageW(combo,CB_SETCURSEL,item,0)==item,"native selection setter succeeds");
                const auto coloured=liveComboColourPixels(combo,evidence/("bypassed-"+std::to_string(round)+"-"+std::to_string(index++)+".bmp"));
                check(coloured==0,"bypassed native combo setter leaves only grayscale client pixels");
                check(probe.directScreenRequests==direct,"bypassed setter never draws an intermediate frame to a screen DC");
                check(changed?probe.requests>requests:probe.requests==requests,
                    "changed selection repaints; unchanged selection keeps its visible pixels intact");
                check(lifetime.alive() && SendMessageW(combo,CB_GETCURSEL,0,0)==item,"repaint preserves native combo lifetime and selection");
                check(s.handler->writes==writes && s.controller->getParamNormalized(10)==algorithm,"paint-only selection emits no automation or sound parameter change");
                ++nativeComboGrayChecks;
            }
            ShowWindow(combo,SW_HIDE);
            SendMessageW(combo,CB_SETCURSEL,selected,0);
            check(!(GetWindowLongPtrW(combo,GWL_STYLE)&WS_VISIBLE),"bypassed setter does not reveal a hidden native child");
            ShowWindow(combo,SW_SHOWNOACTIVATE);
            SendMessageW(combo,WM_SETREDRAW,FALSE,0);
            const auto suspended=GetPropW(combo,L"SysSetRedraw");
            SendMessageW(combo,CB_SETCURSEL,selected,0);
            check(GetPropW(combo,L"SysSetRedraw")==suspended,"bypassed setter preserves an existing redraw suspension");
            SendMessageW(combo,WM_SETREDRAW,TRUE,0);
        }
        HWND checkbox=nullptr;EnumChildWindows(s.window.handle,[](HWND h,LPARAM data)->BOOL{
            wchar_t klass[32]{};GetClassNameW(h,klass,32);
            if(!lstrcmpiW(klass,L"BUTTON") && (GetWindowLongPtrW(h,GWL_STYLE)&BS_TYPEMASK)==BS_AUTOCHECKBOX){*reinterpret_cast<HWND*>(data)=h;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&checkbox));
        check(checkbox && IsWindowVisible(checkbox),"visible Sample peak checkbox for setter regression");
        {
            NativeTextColourProbe probe(checkbox);const auto initial=SendMessageW(checkbox,BM_GETCHECK,0,0);
            for(unsigned i=0;i<4;++i){
                const auto requests=probe.requests,direct=probe.directScreenRequests;
                const auto next=(i%2)?initial:!initial;
                SendMessageW(checkbox,BM_SETCHECK,next,0);
                check(SendMessageW(checkbox,BM_GETCHECK,0,0)==next,"Sample peak setter preserves native check semantics");
                const auto coloured=liveComboColourPixels(checkbox,evidence/("checkbox-"+std::to_string(round)+"-"+std::to_string(i)+".bmp"));
                check(probe.requests>requests,"Sample peak probe observes native text drawing");
                check(coloured==0 && probe.directScreenRequests==direct,"bypassed Sample peak setter has no coloured or direct-screen intermediate paint");
                ++nativeCheckboxGrayChecks;
            }
            const auto requests=probe.requests,direct=probe.directScreenRequests;
            std::wstring text(std::size_t(GetWindowTextLengthW(checkbox))+1,L'\0');
            const int textLength=GetWindowTextW(checkbox,text.data(),int(text.size()));text.resize(textLength);
            for(unsigned i=0;i<30;++i){
                SendMessageW(checkbox,BM_SETCHECK,initial,0);
                SendMessageW(checkbox,WM_SETTEXT,0,reinterpret_cast<LPARAM>(text.c_str()));
                check(IsWindowVisible(checkbox) && SendMessageW(checkbox,BM_GETCHECK,0,0)==initial,
                    "unchanged native refresh retains visible checkbox and state");
            }
            check(probe.requests==requests && probe.directScreenRequests==direct,
                "repeated unchanged checkbox refresh does not repaint or expose parent background");
        }
        ShowWindow(s.window.handle,SW_HIDE);s.close();++nativeComboReopens;
    }
}

void checkNativeBackgroundErase(Session& s,const fs::path& evidence) {
    nativeComboLiveProbe=true;
    auto bytes=[](const fs::path& path){std::ifstream f(path,std::ios::binary|std::ios::ate);auto n=f.tellg();check(n>54,"read native erase pixels");std::vector<char> data(static_cast<std::size_t>(n));f.seekg(0);f.read(data.data(),std::streamsize(data.size()));check(bool(f),"complete native erase capture");return data;};
    for(unsigned round=0;round<3;++round){
        s.open();if(uiState(s.controller).advanced)click(s.window.handle,102);
        if(s.controller->getParamNormalized(0)!=0)click(s.window.handle,101);
        SetWindowPos(s.window.handle,HWND_TOPMOST,0,0,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        HWND button=nullptr;EnumChildWindows(s.window.handle,[](HWND h,LPARAM data)->BOOL{
            wchar_t klass[32]{},caption[32]{};GetClassNameW(h,klass,32);GetWindowTextW(h,caption,32);
            if(IsWindowVisible(h) && !lstrcmpiW(klass,L"BUTTON") && !lstrcmpW(caption,L"Sync")){*reinterpret_cast<HWND*>(data)=h;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&button));
        check(button && (GetWindowLongPtrW(button,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW,"visible native Flanger Sync owner-draw button");
        for(unsigned paused=0;paused<2;++paused){
            if(paused)click(s.window.handle,101);
            RedrawWindow(s.window.handle,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
            const auto before=evidence/("erase-"+std::to_string(round)+"-"+std::to_string(paused)+"-before.bmp");
            const auto after=evidence/("erase-"+std::to_string(round)+"-"+std::to_string(paused)+"-after.bmp");
            liveComboColourPixels(button,before);const auto first=bytes(before);unsigned ink=0;
            for(std::size_t p=54;p+3<first.size();p+=4)if(unsigned(static_cast<unsigned char>(first[p]))+unsigned(static_cast<unsigned char>(first[p+1]))+unsigned(static_cast<unsigned char>(first[p+2]))<600)++ink;
            check(ink>20,"native Sync label is visibly painted before erase");
            const auto writes=s.handler->writes;
            // Observe the exact screen erase between invalidation and buffered
            // WM_PAINT. Capturing only after WM_PAINT would hide a blank frame.
            HDC screen=GetDC(button);check(screen!=nullptr,"owned button screen DC");
            SendMessageW(button,WM_ERASEBKGND,reinterpret_cast<WPARAM>(screen),0);GdiFlush();ReleaseDC(button,screen);
            liveComboColourPixels(button,after);
            if(paused)check(bytes(after)==first,"bypassed native background erase preserves visible button pixels");
            else check(bytes(after)!=first,"active native erase positive control changes screen pixels");
            check(s.handler->writes==writes,"native background handling emits no parameter writes");
            RedrawWindow(button,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);++nativeBackgroundEraseChecks;
        }
        ShowWindow(s.window.handle,SW_HIDE);s.close();
    }
}

void checkNativeAnalysisOverlay(Session& s,const fs::path& evidence) {
    nativeComboLiveProbe=true;
    auto bytes=[](const fs::path& path){std::ifstream f(path,std::ios::binary|std::ios::ate);auto n=f.tellg();check(n>54,"read captured native pixels");std::vector<char> data(static_cast<std::size_t>(n));f.seekg(0);f.read(data.data(),std::streamsize(data.size()));check(bool(f),"complete native pixel capture");return data;};
    for(unsigned round=0;round<3;++round){
        s.open();if(uiState(s.controller).advanced)click(s.window.handle,102);
        if(s.controller->getParamNormalized(0)!=0)click(s.window.handle,101);
        SetWindowPos(s.window.handle,HWND_TOPMOST,0,0,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        HWND graph=nullptr;EnumChildWindows(s.window.handle,[](HWND h,LPARAM data)->BOOL{wchar_t klass[64]{};GetClassNameW(h,klass,64);if(!lstrcmpW(klass,L"JUST.Shared.Analysis.v3")){*reinterpret_cast<HWND*>(data)=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&graph));
        check(graph && IsWindowVisible(graph),"visible native Distortion analysis child");
        HWND notice=nullptr;
        for(HWND h=GetWindow(GetParent(graph),GW_CHILD);h;h=GetWindow(h,GW_HWNDNEXT)){
            wchar_t klass[64]{},text[512]{};GetClassNameW(h,klass,64);GetWindowTextW(h,text,512);
            if(IsWindowVisible(h) && !lstrcmpiW(klass,L"STATIC") && std::wcsstr(text,L"PDC")){notice=h;break;}
        }
        check(notice!=nullptr,"visible native measurement readout exists above graph");
        RECT gr{},nr{},overlap{};GetWindowRect(graph,&gr);GetWindowRect(notice,&nr);
        check(IntersectRect(&overlap,&gr,&nr) && EqualRect(&overlap,&nr),"native readout is fully inside the graph overlap");
        NativeTextColourProbe probe(notice);
        for(unsigned paused=0;paused<2;++paused){
            if(paused)click(s.window.handle,101);
            const auto writes=s.handler->writes;
            RedrawWindow(s.window.handle,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
            RedrawWindow(notice,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
            const auto before=evidence/("readout-"+std::to_string(round)+"-"+std::to_string(paused)+"-before.bmp");
            const auto after=evidence/("readout-"+std::to_string(round)+"-"+std::to_string(paused)+"-after.bmp");
            liveComboColourPixels(notice,before);auto first=bytes(before);unsigned ink=0;
            for(std::size_t p=54;p+3<first.size();p+=4)if(unsigned(static_cast<unsigned char>(first[p]))+unsigned(static_cast<unsigned char>(first[p+1]))+unsigned(static_cast<unsigned char>(first[p+2]))<600)++ink;
            check(probe.requests>0 && ink>20,"measurement text has actual visible ink before graph repaint");
            // Real screen DC paint, deliberately last. Hidden bottom-to-top
            // WM_PRINTCLIENT fixtures cannot expose sibling overpainting.
            RedrawWindow(graph,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
            liveComboColourPixels(notice,after);
            check(bytes(after)==first,"analysis repaint preserves the visible overlapping measurement readout");
            check(s.handler->writes==writes,"analysis sibling clipping emits no sound parameter writes");
            ++nativeAnalysisOverlayChecks;
        }
        ShowWindow(s.window.handle,SW_HIDE);s.close();
    }
}
