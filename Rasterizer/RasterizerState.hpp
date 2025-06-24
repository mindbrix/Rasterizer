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
    enum KeyCode { kC = 8, kF = 3, kI = 34, kL = 37, kO = 31, kP = 35, k1 = 18, k0 = 29, kReturn = 36 };
    enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
    
#pragma mark - Event handlers

    void onFlags(size_t keyFlags) {
        flags = keyFlags;
    }
    bool onKeyDown(unsigned short keyCode, const char *chars) {
        if (keyCode == KeyCode::k1)
            animating = !animating;
        else if (keyCode == KeyCode::k0)
            clock = 0.0;
        else if (keyCode == KeyCode::kC)
            useCurves = !useCurves;
        else if (keyCode == KeyCode::kF)
            ;
        else if (keyCode == KeyCode::kI)
            opaque = !opaque;
        else if (keyCode == KeyCode::kO)
            outlineWidth = outlineWidth ? 0.f : -1.f;
        else if (keyCode == KeyCode::kP)
            mouseMove = !mouseMove, indices = mouseMove ? indices : Ra::Range(INT_MAX, INT_MAX);
        else if (keyCode == KeyCode::kL) {
            locked = locked.begin != INT_MAX ? Ra::Range(INT_MAX, INT_MAX) : indices;
        }
        const KeyCode keyCodes[] = { kC, kF, kI, kO, kP, k0, k1, kL, kReturn };
        for (int code : keyCodes)
            if (code == keyCode) {
                setRedraw();
                return true;
            }
        return false;
    }
    void onKeyUp(unsigned short keyCode, const char *chars) {
    }
    void onFit() {
        fit(list.bounds());
        setRedraw();
    }
    void onMouseMove(float x, float y) {
        mx = x, my = y;
        if (flags & Flags::kShift)
            timeScale = powf(y / (bounds.uy - bounds.ly), 2.0);
        if (mouseMove)
            setRedraw();
    }
    void onMouseDown(float x, float y) {
        mouseDown = true;
    }
    void onMouseUp(float x, float y) {
        mouseDown = false;
    }
    void onMagnify(float scale) {
        if ((flags & Flags::kShift) == 0)
            magnify(scale);
        else
            magnify(scale, mx, my);
        setRedraw();
    }
    void onRotate(float angle) {
        if ((flags & Flags::kShift) == 0)
            rotate(angle);
        else
            rotate(angle, mx, my);
        setRedraw();
    }
    void onDrag(float dx, float dy) {
        translate(dx, dy);
        mx += dx, my += dy;
        setRedraw();
    }
    void onTranslate(float dx, float dy) {
        translate(dx, dy);
        setRedraw();
    }
    
    void onRedraw() {
        redraw = false;
        if (mouseMove)
            indices = RasterizerWinding::indicesForPoint(list, getView(), getDevice(), scale * mx, scale * my);
        if (animating)
            clock += timeScale / 60.0;
    }
    
#pragma mark - View state

    void magnify(float s, float cx = FLT_MAX, float cy = FLT_MAX) {
        cx = cx == FLT_MAX ? bounds.cx() : cx;
        cy = cy == FLT_MAX ? bounds.cy() : cy;
        ctm = ctm.preconcat(Ra::Transform(s, 0.f, 0.f, s, 0.f, 0.f), cx, cy);
    }
    void rotate(float a, float cx = FLT_MAX, float cy = FLT_MAX) {
        cx = cx == FLT_MAX ? bounds.cx() : cx;
        cy = cy == FLT_MAX ? bounds.cy() : cy;
        float sine, cosine;  __sincosf(a, & sine, & cosine);
        ctm = ctm.preconcat(Ra::Transform(cosine, sine, - sine, cosine, 0, 0), cx, cy);
    }
    void translate(float x, float y) {
        ctm.tx += x, ctm.ty += y;
    }
    void fit(Ra::Bounds listBounds) {
        float s = fminf(bounds.width() / listBounds.width(), bounds.height() / listBounds.height());
        Ra::Transform fit = { s, 0.f, 0.f, s, -s * listBounds.lx, -s * listBounds.ly };
        ctm = memcmp(& ctm, & fit, sizeof(ctm)) == 0 ? Ra::Transform() : fit;
    }
   
#pragma mark - Properties
    
    void setViewport(float s, float w, float h) {
        scale = s, bounds = Ra::Bounds(0.f, 0.f, w, h);
    }
    Ra::Transform getView() const {
        return Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(ctm);
    }
    Ra::Bounds getDevice() const {
        return Ra::Bounds(0.f, 0.f, ceilf(scale * bounds.ux), ceilf(scale * bounds.uy));
    }
    void setRedraw() {
        redraw = true;
    }
    bool getShouldRedraw() const {
        return animating || redraw;
    }
    
    Ra::SceneList list;
    
    float scale;
    Ra::Transform ctm;
    Ra::Bounds bounds;
    
    bool redraw = false, mouseDown = false, mouseMove = false, useCurves = true, animating = false, opaque = false;
    double clock = 0.0, timeScale = 0.333;
    float mx, my, outlineWidth = 0.f;
    Ra::Range indices = Ra::Range(INT_MAX, INT_MAX), locked = Ra::Range(INT_MAX, INT_MAX);
    size_t flags = 0;
    
#pragma mark - Static
    
    static void TransferFunction(size_t li, size_t ui, size_t si, Ra::Scene *scn, void *info) {
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
        if (outlineWidth)
            memset_pattern4(dstWidths + li, & outlineWidth, count * sizeof(srcWidths[0]));
        else if (ftime == 0.f)
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
