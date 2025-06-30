//
//  RasterizerState.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 20/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import "RasterizerWinding.hpp"


struct RasterizerState {
    enum KeyCode { kC = 8, kG = 5, kI = 34, kL = 37, kO = 31, kP = 35, kS = 1, kT = 17, k1 = 18, k0 = 29, kReturn = 36, kLeft = 123, kRight = 124 };
    enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
    
    void writeList() {
        Ra::SceneList list;
        Ra::Colorant textColor(0, 0, 0, 255);
        if (pastedString.size) {
            Ra::Scene glyphs;
            RasterizerFont::layoutGlyphs(font, pointSize, 0.f, textColor, bounds, false, false, false, pastedString.addr, glyphs);
            list.addScene(glyphs);
        } else if (showGlyphGrid) {
            list.addScene(RasterizerFont::writeGlyphGrid(font, pointSize, textColor));
        } else if (showTestScenes) {
            list = RasterizerTest::makeConcentrichron(font);
        } else if (svgData.size)
            list.addScene(RasterizerSVG::createScene(svgData.addr, svgData.size));
        else if (pdfData.size)
            list.addList(RasterizerPDF::writeSceneList(pdfData.addr, pdfData.size, pageIndex));

        setList(list);
    }
    
#pragma mark - Event handlers

    void onPaste(const char *string, Ra::Bounds bounds) {
        this->bounds = bounds;
        
        setPastedString(string);
        showGlyphGrid = false;
        showTestScenes = false;
        writeList();
    }
    void onFlags(size_t keyFlags) {
        flags = keyFlags;
        redraw = true;
    }
    bool onKeyDown(unsigned short keyCode) {
        bool keyUsed = false;
        
        if (keyCode == KeyCode::k1)
            animating = !animating, keyUsed = true;
        else if (keyCode == KeyCode::k0)
            clock = 0.0, keyUsed = true;
        else if (keyCode == KeyCode::kC)
            useCurves = !useCurves, keyUsed = true;
        else if (keyCode == KeyCode::kReturn) {
            Ra::Bounds listBounds = list.bounds();
            float s = fminf(bounds.width() / listBounds.width(), bounds.height() / listBounds.height());
            Ra::Transform fit = { s, 0.f, 0.f, s, -s * listBounds.lx, -s * listBounds.ly };
            ctm = memcmp(& ctm, & fit, sizeof(ctm)) == 0 ? Ra::Transform() : fit;
            keyUsed = true;
        } else if (keyCode == KeyCode::kI)
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
            showTestScenes = !showTestScenes;
            setPastedString(nullptr);
            writeList();
            keyUsed = true;
        } else if (keyCode == KeyCode::kG) {
            showTestScenes = false;
            showGlyphGrid = !showGlyphGrid;
            setPastedString(nullptr);
            writeList();
            keyUsed = true;
        } else if (keyCode == KeyCode::kLeft) {
            if (pageIndex > 0) {
                pageIndex--;
                writeList();
            }
            keyUsed = true;
        } else if (keyCode == KeyCode::kRight) {
            pageIndex++;
            writeList();
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
        if (flags & Flags::kShift)
            timeScale = powf(y / (bounds.uy - bounds.ly), 2.0);
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
    
    void onRedraw(float s, float w, float h) {
        scale = s, bounds = Ra::Bounds(0.f, 0.f, w, h);
        redraw = false;
        if (animating)
            clock += timeScale / 60.0;
        runTransferFunction(list, transferFunction, this);
        if (mouseMove)
            indices = RasterizerWinding::indicesForPoint(list, getView(), Ra::Bounds(0.f, 0.f, ceilf(s * w), ceilf(s * h)), s * mx, s * my);
    }
   
#pragma mark - Properties
    
    Ra::Transform getView() const {
        return Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(ctm);
    }
    bool getShouldRedraw() const {
        return animating || redraw;
    }
    void setFont(const char *url, const char *name, float size) {
        pointSize = size;
        font.load(url, name);
    }
    void setList(Ra::SceneList list) {
        this->list = list, redraw = true;
    }
    void setPastedString(const char *string) {
        if (string)
            strcpy((char *)pastedString.resize(strlen(string) + 1), string);
        else
            pastedString = Ra::Memory<char>();
    }
    void setPdfData(const void *data, size_t size) {
        if (data)
            memcpy(pdfData.resize(size), data, size);
    }
    void setSvgData(const void *data, size_t size) {
        if (data)
            memcpy(svgData.resize(size), data, size);
    }
    
    RasterizerFont font;
    float pointSize = 14;
    
    Ra::SceneList list;
    
    Ra::Memory<char> pastedString;
    bool showGlyphGrid = false, showTestScenes = false;
    size_t pageIndex = 0;
    Ra::Memory<uint8_t> pdfData, svgData;
    
    float scale;
    Ra::Transform ctm;
    Ra::Bounds bounds;

    bool redraw = false, mouseDown = false, mouseMove = false, useCurves = true, animating = false, opaque = false;
    double clock = 0.0, timeScale = 0.333;
    float mx, my, outlineWidth = 0.f;
    Ra::Range indices = Ra::Range(INT_MAX, INT_MAX), locked = Ra::Range(INT_MAX, INT_MAX);
    size_t flags = 0;
    
#pragma mark - Static
    typedef void (*TransferFunction)(size_t li, size_t ui, size_t si, Ra::Scene *scn, Ra::Transform ctm, void *info);
    
    
    static void runTransferFunction(Ra::SceneList& list, TransferFunction function, void *state) {
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
                (*function)(divisions[i], divisions[i + 1], si, scn, ctm, state);
            });
        }
    }
    
    static void transferFunction(size_t li, size_t ui, size_t si, Ra::Scene *scn, Ra::Transform ctm, void *info) {
        Ra::Bounds *bounds = scn->bnds.base;
        Ra::Transform *srcCtms = scn->ctms->src.base, *dstCtms = scn->ctms->base;
        Ra::Colorant *srcColors = scn->colors->src.base, *dstColors = scn->colors->base;
        float *srcWidths = scn->widths->src.base, *dstWidths = scn->widths->base;
        uint8_t *srcFlags = scn->flags->src.base, *dstFlags = scn->flags->base;
        RasterizerState& state = *((RasterizerState *)info);
        size_t count = ui - li;
        Ra::Colorant black(0, 0, 0, 255), red(0, 0, 255, 255);
        const float kScaleMin = 1.0f, kScaleMax = 1.2f;
        float ftime = state.clock - floor(state.clock);
        float t = sinf(kTau * ftime), s = 1.f - t;
        float scale = s * kScaleMin + t * kScaleMax, outlineWidth = state.outlineWidth;
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
            float scale = fabsf(outlineWidth) / state.getView().concat(ctm).scale();
            for (size_t j = li; j < ui; j++)
                dstWidths[j] = scale / dstCtms[j].scale();
        } else if (ftime == 0.f)
            memcpy(dstWidths + li, srcWidths + li, count * sizeof(srcWidths[0]));
        else
            for (size_t j = li; j < ui; j++)
                dstWidths[j] = scale * srcWidths[j];
        
        for (size_t j = li; j < ui; j++) {
            dstColors[j] = (state.indices.begin == si && state.indices.end == j) ? red : state.outlineWidth != 0.f ? (srcWidths[j] ? red : black) : srcColors[j];
            if (state.opaque)
                dstColors[j].a = 255;
        }
        for (size_t j = li; j < ui; j++) {
            dstFlags[j] = state.locked.begin == INT_MAX ? srcFlags[j] : si == state.locked.begin && j == state.locked.end ? srcFlags[j] & ~Ra::Scene::kInvisible : srcFlags[j] | Ra::Scene::kInvisible;
        }
    }
};

typedef RasterizerState RaSt;
