using SanginRect = Rect;
// Native GDI+ rendering of sangin_webui/index.html and its CSS design.
// No browser, HTTP server or web runtime is used by this screen.
void draw_sangin_scene(HDC hdc) {
    using namespace Gdiplus;
    layout();
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    const Color cream(245,244,232), muted(214,224,200), lime(129,182,76), ink(38,49,38);
    auto box = [&](SanginRect r, Color c, int radius = 0) {
        SolidBrush brush(c);
        if (!radius) { g.FillRectangle(&brush,r.x,r.y,r.w,r.h); return; }
        GraphicsPath path;
        const int d = radius * 2;
        path.AddArc(r.x,r.y,d,d,180,90); path.AddArc(r.x+r.w-d,r.y,d,d,270,90);
        path.AddArc(r.x+r.w-d,r.y+r.h-d,d,d,0,90); path.AddArc(r.x,r.y+r.h-d,d,d,90,90);
        path.CloseFigure(); g.FillPath(&brush,&path);
    };
    auto label = [&](const std::wstring& value, SanginRect r, int size, Color color,
                     bool bold = false, bool centered = false, bool serif = false) {
        FontFamily family(serif ? L"Georgia" : L"Segoe UI");
        Font font(&family,static_cast<REAL>(size),bold ? FontStyleBold : FontStyleRegular,UnitPixel);
        SolidBrush brush(color);
        StringFormat format;
        if (centered) { format.SetAlignment(StringAlignmentCenter); format.SetLineAlignment(StringAlignmentCenter); }
        g.DrawString(value.c_str(),-1,&font,RectF(static_cast<REAL>(r.x),static_cast<REAL>(r.y),
            static_cast<REAL>(r.w),static_cast<REAL>(r.h)),&format,&brush);
    };
    const int panel_width = clamp(ui.w * 27 / 100,310,420), panel_x = ui.w-panel_width;
    box({0,0,ui.w,ui.h},Color(42,82,50));
    box({panel_x,0,panel_width,ui.h},Color(37,72,45));
    GraphicsPath board_path;
    const auto b = ui.board;
    board_path.AddRectangle(Gdiplus::Rect(b.x,b.y,b.w,b.h));
    LinearGradientBrush wood(Point(b.x,b.y),Point(b.x+b.w,b.y+b.h),Color(241,198,129),Color(221,165,94));
    g.FillPath(&wood,&board_path);
    const int gx=b.x+ui.margin, gy=b.y+ui.margin, span=ui.cell*14;
    Pen grid(Color(86,62,40),1);
    for(int i=0;i<15;++i) {
        g.DrawLine(&grid,gx,gy+i*ui.cell,gx+span,gy+i*ui.cell);
        g.DrawLine(&grid,gx+i*ui.cell,gy,gx+i*ui.cell,gy+span);
    }
    SolidBrush star(Color(77,59,37));
    for(int r : {3,7,11}) for(int c : {3,7,11})
        g.FillEllipse(&star,gx+c*ui.cell-3,gy+r*ui.cell-3,6,6);
    for(int r=0;r<15;++r) for(int c=0;c<15;++c) {
        int stone=displayed_cell(r,c); if(!stone) continue;
        const int x=gx+c*ui.cell, y=gy+r*ui.cell, rad=max(7,ui.cell/2-3);
        GraphicsPath path; path.AddEllipse(x-rad,y-rad,rad*2,rad*2);
        PathGradientBrush shade(&path);
        shade.SetCenterPoint(PointF(x-rad*.35f,y-rad*.45f));
        shade.SetCenterColor(stone==1?Color(65,68,65):Color(255,254,247));
        Color edge=stone==1?Color(11,12,11):Color(215,214,201);
        int count=1; shade.SetSurroundColors(&edge,&count); g.FillPath(&shade,&path);
    }
    auto last = last_move;
    if(review_mode) last=review_ply>0?log_v[review_ply-1]:pair<int,int>{-1,-1};
    if(last.first>=0) {
        Pen marker(lime,2); int rad=max(7,ui.cell/2-2);
        g.DrawEllipse(&marker,gx+last.second*ui.cell-rad,gy+last.first*ui.cell-rad,rad*2,rad*2);
    }
    if(forbidden_warn.first>=0 && forbidden_warning_phase()>=0) {
        SolidBrush warning(Color(210,217,75,69)); int rad=max(7,ui.cell/2-3);
        g.FillEllipse(&warning,gx+forbidden_warn.second*ui.cell-rad,gy+forbidden_warn.first*ui.cell-rad,rad*2,rad*2);
    }
    if(win_line_start.first>=0 && (!review_mode || review_ply==(int)log_v.size())) {
        Pen win(lime,5);
        g.DrawLine(&win,gx+win_line_start.second*ui.cell,gy+win_line_start.first*ui.cell,
            gx+win_line_end.second*ui.cell,gy+win_line_end.first*ui.cell);
    }
    int padding=clamp(ui.w*25/1000,24,38), x=panel_x+padding, width=panel_width-2*padding;
    int top=clamp(ui.h*4/100,30,48);
    label(L"15×15 · RENJU",{x,top,width,18},11,lime,true);
    label(L"SANGIN Gomoku",{x,top+18,width,110},clamp(ui.w*44/1000,42,62),cream,true,false,true);
    SolidBrush black(Color(20,21,20)),white(Color(242,238,225));
    g.FillEllipse(&black,x,top+143,14,14); g.FillEllipse(&white,x+91,top+143,14,14);
    label(L"나 · 흑",{x+21,top+140,65,22},12,muted,true);
    label(L"RAPFI 5 · 백",{x+112,top+140,width-112,22},12,muted,true);
    Pen divider(Color(41,214,224,200),1);
    g.DrawLine(&divider,x,top+181,x+width,top+181);
    g.DrawLine(&divider,x,top+235,x+width,top+235);
    std::wstring status=review_mode?L"복기":game_over?(winner==0?L"무승부":winner==1?L"승리":L"패배"):ai_thinking?L"계산 중…":L"내 차례";
    label(status,{x,top+192,width-65,38},clamp(ui.w*2/100,20,27),lime,true);
    label(std::to_wstring(log_v.size())+L"수",{x+width-52,top+198,52,25},11,muted,true,true);
    box({x,top+253,width,42},Color(31,60,38),6);
    label(L"AI",{x+14,top+265,24,22},12,muted);
    label(L"RAPFI 2025 · LEVEL 5",{x+42,top+265,width-49,22},14,cream,true);
    // Match the WebUI's 14px gap between fixed engine and action controls.

    for(int i : {0,1,2,5}) {
        if(!review_mode && i!=0) continue;
        SanginRect button=ui.buttons[i]; box(button,Color(238,238,228),6);
        label(review_mode?(i==0?L"이전":i==1?L"다음":i==2?L"마지막":L"새 대국"):L"새 대국",button,15,ink,true,true);
    }
    if(review_mode) {
        int y=ui.buttons[5].y+71;
        label(L"수순 "+std::to_wstring(review_ply)+L"/"+std::to_wstring(log_v.size()),{x,y,width/2,25},12,cream,true);
        if(review_ply>0 && review_ply<=(int)reviews.size()) {
            const auto& move=reviews[review_ply-1];
            label(std::wstring(move.label.begin(),move.label.end()),{x+width/2,y,width/2,28},17,lime,true);
            label(L"흑 승률 "+std::to_wstring((int)move.winrate)+L"%",{x+width/2,y+28,width/2,24},11,muted);
        }
        SanginRect chart{x,y+58,width,126};
        box({chart.x,chart.y,chart.w,63},Color(49,50,47));
        box({chart.x,chart.y+63,chart.w,63},Color(225,224,214));
        Pen curve(Color(240,181,54),2);
        for(size_t i=1;i<reviews.size();++i)
            g.DrawLine(&curve,x+(int)((i-1)*width/(reviews.size()-1)),chart.y+126-(int)(reviews[i-1].winrate*1.26),
                x+(int)(i*width/(reviews.size()-1)),chart.y+126-(int)(reviews[i].winrate*1.26));
        if(review_ply>0 && !reviews.empty()) {
            int cursor=x+(reviews.size()==1?width/2:(review_ply-1)*width/(reviews.size()-1));
            Pen selected(lime,2); g.DrawLine(&selected,cursor,chart.y,cursor,chart.y+126);
        }
    }
    label(L"빈 교차점을 누르면 흑돌이 즉시 놓입니다.",{x,ui.h-46,width,36},12,muted);
    if(sangin_rules_visible) {
        box({0,0,ui.w,ui.h},Color(199,10,20,12));
        SanginRect card{(ui.w-510)/2,(ui.h-370)/2,510,370};
        box(card,Color(37,72,45),12);
        int cx=card.x+42,cy=card.y+34;
        label(L"대국 전 안내",{cx,cy,426,20},11,lime,true);
        label(L"Renju 규칙",{cx,cy+24,426,58},44,cream,true,false,true);
        label(L"• 흑이 먼저 두며, 흑은 정확히 5목을 만들면 승리합니다.\n"
              L"• 흑의 장목(6목 이상), 3-3, 4-4는 금수라 둘 수 없습니다.\n"
              L"• 백은 5목 이상을 이으면 승리하며 금수 제한이 없습니다.",
              {cx,cy+90,426,130},16,cream);
        label(L"15×15 고정 · Rapfi 레벨 5 · 히트맵 없음",{cx,cy+222,426,25},14,muted);
        SanginRect start=sangin_rules_button(); box(start,lime,6);
        label(L"대국 시작",start,16,Color(24,35,24),true,true);
    }
}

// Exercise the actual native layout, hit testing and engine without opening a window.
int sangin_smoke_test(const filesystem::path& output) {
    filesystem::create_directories(output);
    ui.w=1280; ui.h=900;
    HDC dc=CreateCompatibleDC(nullptr);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=ui.w; info.bmiHeader.biHeight=-ui.h;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    HGDIOBJ previous=SelectObject(dc,bitmap);
    auto snapshot=[&](const wchar_t* name) {
        draw_sangin_scene(dc);
        Gdiplus::Bitmap image(bitmap,nullptr);
        UINT count=0,bytes=0;
        Gdiplus::GetImageEncodersSize(&count,&bytes);
        vector<unsigned char> data(bytes);
        auto encoders=reinterpret_cast<Gdiplus::ImageCodecInfo*>(data.data());
        Gdiplus::GetImageEncoders(count,bytes,encoders);
        for(UINT i=0;i<count;++i) if(wcscmp(encoders[i].MimeType,L"image/png")==0)
            return image.Save((output/name).c_str(),&encoders[i].Clsid,nullptr)==Gdiplus::Ok;
        return false;
    };
    bool passed=sangin_rules_visible && snapshot(L"rules.png");
    handle_click(ui.board.x+ui.margin,ui.board.y+ui.margin);
    passed=passed && log_v.empty();
    Rect start=sangin_rules_button();
    handle_click(start.x+start.w/2,start.y+start.h/2);
    passed=passed && !sangin_rules_visible && snapshot(L"ready.png");
    handle_click(ui.board.x+ui.margin+7*ui.cell,ui.board.y+ui.margin+7*ui.cell);
    passed=passed && log_v.size()==1 && brd[7][7]==1 && snapshot(L"first-move.png");
    auto deadline=chrono::steady_clock::now()+chrono::seconds(90);
    while(ai_thinking && chrono::steady_clock::now()<deadline) {
        this_thread::sleep_for(chrono::milliseconds(20)); apply_pending_ai_move();
    }
    passed=passed && !ai_thinking && sangin_external_reply.load() && log_v.size()==2 && snapshot(L"ai-reply.png");
    compute_reviews(); review_mode=true; review_ply=static_cast<int>(log_v.size());
    passed=passed && snapshot(L"review.png");
    handle_click(ui.buttons[0].x+10,ui.buttons[0].y+10);
    passed=passed && review_ply==1;
    handle_click(ui.buttons[5].x+10,ui.buttons[5].y+10);
    passed=passed && sangin_rules_visible && log_v.empty();
    SelectObject(dc,previous); DeleteObject(bitmap); DeleteDC(dc);
    fprintf(stdout,"[%s] native Sangin: rules gate, start, immediate stone, AI reply, review, new game\n",passed?"PASS":"FAIL");
    return passed?0:1;
}
