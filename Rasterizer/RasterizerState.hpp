//
//  RasterizerState.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 20/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import "RasterizerWinding.hpp"

struct ViewState {
    void setViewport(float s, float w, float h) {
        scale = s, bounds = Ra::Bounds(0.f, 0.f, w, h);
    }
    Ra::Transform getView() {
        return Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(ctm);
    }
    Ra::Bounds getDevice() {
        return Ra::Bounds(0.f, 0.f, ceilf(scale * bounds.ux), ceilf(scale * bounds.uy));
    }
    void magnify(float s, float cx = FLT_MAX, float cy = FLT_MAX) {
        cx = cx == FLT_MAX ? 0.5f * (bounds.lx + bounds.ux) : cx;
        cy = cy == FLT_MAX ? 0.5f * (bounds.ly + bounds.uy) : cy;
        ctm = ctm.preconcat(Ra::Transform(s, 0.f, 0.f, s, 0.f, 0.f), cx, cy);
    }
    void rotate(float a, float cx = FLT_MAX, float cy = FLT_MAX) {
        cx = cx == FLT_MAX ? 0.5f * (bounds.lx + bounds.ux) : cx;
        cy = cy == FLT_MAX ? 0.5f * (bounds.ly + bounds.uy) : cy;
        float sine, cosine;  __sincosf(a, & sine, & cosine);
        ctm = ctm.preconcat(Ra::Transform(cosine, sine, - sine, cosine, 0, 0), cx, cy);
    }
    void translate(float x, float y) {
        ctm.tx += x, ctm.ty += y;
    }
    void fit(Ra::Bounds b) {
        float s = fminf((bounds.ux - bounds.lx) / (b.ux - b.lx), (bounds.uy - bounds.ly) / (b.uy - b.ly));
        Ra::Transform fit = { s, 0.f, 0.f, s, -s * b.lx, -s * b.ly };
        ctm = memcmp(& ctm, & fit, sizeof(ctm)) == 0 ? Ra::Transform() : fit;
    }
    
    float scale;
    Ra::Transform ctm;
    Ra::Bounds bounds;
};

struct RasterizerState: ViewState {
    enum KeyCode { kC = 8, kF = 3, kI = 34, kL = 37, kO = 31, kP = 35, k1 = 18, k0 = 29, kReturn = 36 };
    enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
    
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
    void onFit(Ra::Bounds b) {
        fit(b);
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
    void setRedraw() {
        redraw = true;
    }
    
    void onRedraw(Ra::SceneList& list) {
        redraw = false;
        if (mouseMove)
            indices = RasterizerWinding::indicesForPoint(list, getView(), getDevice(), scale * mx, scale * my);
        if (animating)
            clock += timeScale / 60.0;
    }
    
    bool needsRedraw() {  return animating || redraw;  }
    
    bool redraw = false, mouseDown = false, mouseMove = false, useCurves = true, animating = false, opaque = false;
    double clock = 0.0, timeScale = 0.333;
    float mx, my, outlineWidth = 0.f;
    Ra::Range indices = Ra::Range(INT_MAX, INT_MAX), locked = Ra::Range(INT_MAX, INT_MAX);
    size_t flags = 0;
};

typedef RasterizerState RaSt;
