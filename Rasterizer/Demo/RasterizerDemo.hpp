//
//  Copyright 2025 Nigel Timothy Barber
//

#import "Rasterizer.hpp"
#import "RasterizerPDF.hpp"
#import "RasterizerSVG.hpp"
#import "RasterizerFont.hpp"
#import "Concentrichron.hpp"
#import "RasterizerWinding.hpp"


struct RasterizerDemo {
    constexpr static float kHudWidth = 240, kHudHeight = 240, kHudInset = 20, kHudBorder = 0.5;
    constexpr static size_t kHudItemCount = 11;
    
    struct HudItem {
        HudItem(char const *key, char const *text, char const *alt = nullptr) : key(key), text(text), alt(alt) {}
        char const *key, *text, *alt;
    };
    
    HudItem hudItems[kHudItemCount] = {
        HudItem("0", "Rasterizer", "Core Graphics"),
        HudItem("A", "Animate"),
        HudItem("C", "Curves"),
        HudItem("F", "Fit bounds"),
        HudItem("G", "Glyph grid"),
        HudItem("H", "HUD"),
        HudItem("I", "Opaque"),
        HudItem("O", "Outlines"),
        HudItem("P", "Path mouseover"),
        HudItem("S", "PDF screenshot"),
        HudItem("T", "Time"),
    };
    
    enum KeyCode { kA = 0, kC = 8, kF = 3, kG = 5, kH = 4, kI = 34, kL = 37, kO = 31, kP = 35, kS = 1, kT = 17, k1 = 18, k0 = 29, kLeft = 123, kRight = 124 };
    enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
    
    void writeList(Ra::Bounds bounds) {
        list.empty();
        if (pastedString.size) {
            if (pasted.pathsCount == 0) {
                Ra::Scene glyphs;
                font.layoutGlyphs(pointSize, 0.f, textColor, bounds, false, false, false, pastedString.addr, glyphs);
                pasted.addScene(glyphs);
            }
            list.addList(pasted);
        } else if (showGlyphGrid) {
            if (text.pathsCount == 0)
                text.addScene(font.writeGlyphGrid(pointSize, textColor));
            list.addList(text);
        } else if (showTime) {
            list.addList(concentrichron.writeList(font));
        } else if (svgData.size) {
            if (document.pathsCount == 0)
                document.addScene(RasterizerSVG::createScene(svgData.addr, svgData.size)), fit = true;
            list.addList(document);
        } else if (pdfData.size) {
            if (document.pathsCount == 0)
                document.addList(RasterizerPDF::writeSceneList(pdfData.addr, pdfData.size, pageIndex)), fit = true;
            list.addList(document);
        }
    }
    
#pragma mark - Event handlers

    void onPaste(const char *string, Ra::Bounds bounds) {
        this->bounds = bounds;
        
        showGlyphGrid = false;
        showTime = false;
        setPastedString(string);
    }
    void onFlags(size_t keyFlags) {
        flags = keyFlags;
        redraw = true;
    }
    bool onKeyDown(unsigned short keyCode, size_t keyFlags) {
        if (keyFlags)
            return false;
        
        bool keyUsed = false;
        if (keyCode == KeyCode::kA)
            animating = !animating, clock = 0.0, keyUsed = true;
        else if (keyCode == KeyCode::kC)
            useCurves = !useCurves, keyUsed = true;
        else if (keyCode == KeyCode::kF) {
            Ra::Transform fit = bounds.fit(list.bounds());
            ctm = memcmp(& ctm, & fit, sizeof(ctm)) == 0 ? Ra::Transform() : fit;
            keyUsed = true;
        } else if (keyCode == KeyCode::kH)
            showHud = !showHud, keyUsed = true;
        else if (keyCode == KeyCode::kI)
            opaque = !opaque, keyUsed = true;
        else if (keyCode == KeyCode::kO)
            outlineWidth = outlineWidth ? 0.f : -1.f, keyUsed = true;
        else if (keyCode == KeyCode::kP)
            mouseMove = !mouseMove, indices = mouseMove ? indices : Ra::Range(INT_MAX, INT_MAX), keyUsed = true;
        else if (keyCode == KeyCode::kL)
            locked = locked.begin != INT_MAX ? Ra::Range(INT_MAX, INT_MAX) : indices, keyUsed = true;
        else if (keyCode == KeyCode::kS)
            RaCG::screenGrabToPDF(list, ctm, bounds), keyUsed = true;
        else if (keyCode == KeyCode::kT) {
            showGlyphGrid = false;
            showTime = !showTime;
            setPastedString(nullptr);
            keyUsed = true;
        } else if (keyCode == KeyCode::kG) {
            showTime = false;
            showGlyphGrid = !showGlyphGrid;
            setPastedString(nullptr);
            keyUsed = true;
        } else if (keyCode == KeyCode::kLeft) {
            if (pageIndex > 0) {
                pageIndex--;
                document.empty();
            }
            keyUsed = true;
        } else if (keyCode == KeyCode::kRight) {
            if (pageIndex < pageCount - 1) {
                pageIndex++;
                document.empty();
            }
            keyUsed = true;
        }
        if (keyUsed)
            redraw = true;
        return keyUsed;
    }
    void onKeyUp(unsigned short keyCode) {
    }
    void onMouseMove(float x, float y) {
        mx = x, my = y;
        if (mouseMove)
            redraw = true;
    }
    void onMouseDown(float x, float y) {
        mouseDown = true;
    }
    void onMouseUp(float x, float y) {
        mouseDown = false;
    }
    void onMagnify(float s) {
        float cx = (flags & Flags::kShift) ? mx : bounds.cx();
        float cy = (flags & Flags::kShift) ? my : bounds.cy();
        ctm = ctm.preconcat(Ra::Transform(s, 0.f, 0.f, s, 0.f, 0.f), cx, cy);
        redraw = true;
    }
    void onRotate(float a) {
        float cx = (flags & Flags::kShift) ? mx : bounds.cx();
        float cy = (flags & Flags::kShift) ? my : bounds.cy();
        float sine, cosine;  __sincosf(a, & sine, & cosine);
        ctm = ctm.preconcat(Ra::Transform(cosine, sine, - sine, cosine, 0, 0), cx, cy);
        redraw = true;
    }
    void onDrag(float dx, float dy) {
        ctm.tx += dx, ctm.ty += dy;
        mx += dx, my += dy;
        redraw = true;
    }
    void onTranslate(float dx, float dy) {
        ctm.tx += dx, ctm.ty += dy;
        redraw = true;
    }
    
    void onRedraw(float w, float h) {
        bounds = Ra::Bounds(0.f, 0.f, w, h);
        redraw = false;
        if (animating)
            clock += timeScale / 60.0;
        if (mouseMove)
            indices = RasterizerWinding::indicesForPoint(list, ctm, bounds, mx, my);
    }
   
#pragma mark - Properties
    
    Ra::Scene getHUD() {
        Ra::Scene hud;
        
        Ra::Bounds bnds = Ra::Bounds(0, 0, kHudWidth, kHudHeight);
        float padding = 0.666 * bnds.height() / (kHudItemCount + 2);
        Ra::Bounds text = bnds.inset(padding, 0.666 * padding);
        
        Ra::Path p;  p->addBounds(bnds.inset(0.5 * kHudBorder, 0.5 * kHudBorder)), p->close();
        hud.addPath(p, Ra::Transform(), bgColor, 0, 0);
        
        float lineHeight = text.height() / kHudItemCount, uy;
        float emSize = lineHeight * float(font.unitsPerEm) / (font.ascent - font.descent + font.lineGap);
        for (size_t i = 0; i < kHudItemCount; i++) {
            HudItem& item = hudItems[i];
            uy = text.uy - i * lineHeight;
            Ra::Colorant color = textColor;
            if (  (*item.key == '0')
                || (*item.key == 'A' && animating)
                || (*item.key == 'G' && showGlyphGrid)
                || (*item.key == 'I' && opaque)
                || (*item.key == 'O' && outlineWidth != 0)
                || (*item.key == 'P' && mouseMove)
                || (*item.key == 'T' && showTime)
                || (*item.key == 'C' && useCurves))
                color = activeColor;
            font.layoutGlyphs(emSize, 0, textColor, Ra::Bounds(text.lx, text.ly, text.ux, uy), false, true, false, item.key, hud);
            char const *label = item.text;
            if (*item.key == '0' && !gpu)
                label = item.alt;
            font.layoutGlyphs(emSize, 0, color, Ra::Bounds(text.lx + 2 * emSize, text.ly, text.ux, uy), false, true, false, label, hud);
        }
        hud.addPath(p, Ra::Transform(), textColor, kHudBorder, 0);
        return hud;
    }
    Ra::DrawList getDrawList(float w, float h) {
        bounds = Ra::Bounds(0.f, 0.f, w, h);
        writeList(bounds);
        runTransferFunction(list, transferFunction, this);
        if (fit)
            ctm = bounds.fit(list.bounds()), fit = false;
        Ra::DrawList draw;
        draw.list = list, draw.ctm = ctm, draw.useCurves = useCurves;
        if (showHud) {
            Ra::Bounds clip = Ra::Bounds(0, 0, kHudWidth, kHudHeight);
            Ra::Transform m = ctm.invert().concat(Ra::Transform(1, 0, 0, 1, kHudInset, bounds.uy - kHudInset - kHudHeight));
            draw.list.addScene(getHUD(), m, clip);
        }
        return draw;
    }
    bool getShouldRedraw() const {
        return animating || redraw || showTime;
    }
    void setFont(const char *url, const char *name, float size) {
        pointSize = size;
        font.load(url, name);
        concentrichron.resetFace();
        pasted.empty();
        text.empty();
        redraw = true;
    }
    void setPastedString(const char *string) {
        if (string)
            strcpy((char *)pastedString.resize(strlen(string) + 1), string);
        else
            pastedString = Ra::Memory<char>();
        pasted.empty();
        redraw = true;
    }
    void setPdfData(const void *data, size_t size) {
        if (data) {
            memcpy(pdfData.resize(size), data, size);
            pageCount = RasterizerPDF::getPageCount(data, size);
            pageIndex = 0;
        }
        redraw = true;
    }
    void setSvgData(const void *data, size_t size) {
        if (data)
            memcpy(svgData.resize(size), data, size);
        redraw = true;
    }
    void setUseGPU(bool useGPU) {
        gpu = useGPU;
        redraw = true;
    }
    
    Ra::Colorant textColor = Ra::Colorant(0, 0, 0, 255), activeColor = Ra::Colorant(0, 0, 255, 255), bgColor = Ra::Colorant(255, 255, 255, 192);
    RasterizerFont font;
    float pointSize = 14;
    Concentrichron concentrichron;
    Ra::SceneList list, document, pasted, text;
    Ra::Memory<char> pastedString;
    bool showGlyphGrid = false, showTime = false, showHud = true;
    size_t pageCount, pageIndex;
    Ra::Memory<uint8_t> pdfData, svgData;
    
    Ra::Transform ctm;
    Ra::Bounds bounds;

    bool gpu = true, redraw = false, fit = false, mouseDown = false, mouseMove = false, useCurves = true, animating = false, opaque = false;
    double clock = 0.0, timeScale = 0.333;
    float mx, my, outlineWidth = 0.f;
    Ra::Range indices = Ra::Range(INT_MAX, INT_MAX), locked = Ra::Range(INT_MAX, INT_MAX);
    size_t flags = 0;
    
#pragma mark - Static
    typedef void (*TransferFunction)(size_t li, size_t ui, size_t si, Ra::Scene *scn, Ra::Transform ctm, void *info);
    
    
    static void runTransferFunction(Ra::SceneList& list, TransferFunction function, void *info) {
        if (function == nullptr)
            return;
        for (int si = 0; si < list.scenes.size(); si++) {
            int threads = 8;
            Ra::Scene *scn = & list.scenes[si];
            Ra::Transform ctm = list.ctms[si];
            std::vector<size_t> divisions;
            divisions.emplace_back(0);
            for (int i = 0; i < threads; i++)
                divisions.emplace_back(ceilf(float(i + 1) / float(threads) * float(scn->count)));
            dispatch_apply(threads, DISPATCH_APPLY_AUTO, ^(size_t i) {
                (*function)(divisions[i], divisions[i + 1], si, scn, ctm, info);
            });
        }
    }
    
    static void transferFunction(size_t li, size_t ui, size_t si, Ra::Scene *scn, Ra::Transform ctm, void *info) {
        RasterizerDemo& demo = *((RasterizerDemo *)info);
        Ra::Bounds *bounds = scn->bnds.base;
        Ra::Transform *srcCtms = scn->ctms->src.base, *dstCtms = scn->ctms->base;
        Ra::Colorant *srcColors = scn->colors->src.base, *dstColors = scn->colors->base;
        float *srcWidths = scn->widths->src.base, *dstWidths = scn->widths->base;
        uint8_t *srcFlags = scn->flags->src.base, *dstFlags = scn->flags->base;
        size_t count = ui - li;
        Ra::Colorant black(0, 0, 0, 255), red(0, 0, 255, 255);
        const float kScaleMin = 1.0f, kScaleMax = 1.2f;
        float ftime = demo.clock - floor(demo.clock);
        float t = sinf(kTau * ftime), s = 1.f - t;
        float scale = s * kScaleMin + t * kScaleMax, outlineWidth = demo.outlineWidth;
        if (0 && outlineWidth) {
            black = Ra::Colorant(0, 0, 0, 64), red = Ra::Colorant(0, 0, 255, 64), outlineWidth = -20.f;
        }
        if (ftime == 0.f)
            memcpy(dstCtms + li, srcCtms + li, count * sizeof(srcCtms[0]));
        else {
            float cx, cy, cos0 = cosf(M_PI * t), sin0 = sinf(M_PI * t), cos1 = cosf(M_PI * -t), sin1 = sinf(M_PI * -t);
            Ra::Transform rsts[2] = {
                { scale * cos0, scale * sin0, scale * -sin0, scale * cos0, 0, 0 },
                { scale * cos1, scale * sin1, scale * -sin1, scale * cos1, 0, 0 }};
            Ra::Transform *m = srcCtms + li;  Ra::Bounds *b = bounds + li;
            for (size_t j = li; j < ui; j++, m++, b++) {
                cx = 0.5f * (b->lx + b->ux), cy = 0.5f * (b->ly + b->uy);
                dstCtms[j] = m->preconcat(rsts[j & 1], cx * m->a + cy * m->c + m->tx, cx * m->b + cy * m->d + m->ty);
            }
        }
        if (outlineWidth) {
            for (size_t j = li; j < ui; j++)
                dstWidths[j] = outlineWidth;
        } else if (ftime == 0.f)
            memcpy(dstWidths + li, srcWidths + li, count * sizeof(srcWidths[0]));
        else
            for (size_t j = li; j < ui; j++)
                dstWidths[j] = scale * srcWidths[j];
        
        for (size_t j = li; j < ui; j++) {
            dstColors[j] = (demo.indices.begin == si && demo.indices.end == j) ? red : demo.outlineWidth != 0.f ? (srcWidths[j] ? red : black) : srcColors[j];
            if (demo.opaque)
                dstColors[j].a = 255;
        }
        for (size_t j = li; j < ui; j++) {
            dstFlags[j] = demo.locked.begin == INT_MAX ? srcFlags[j] : si == demo.locked.begin && j == demo.locked.end ? srcFlags[j] & ~Ra::Scene::kInvisible : srcFlags[j] | Ra::Scene::kInvisible;
        }
    }
};
