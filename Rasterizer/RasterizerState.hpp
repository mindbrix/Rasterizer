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
    struct Event {
        enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
        enum Type { kNull = 0, kMouseMove, kMouseUp, kDragged, kMouseDown, kFlags, kKeyDown, kKeyUp, kMagnify, kRotate, kTranslate, kFit };
        
        Event() {}
        Event(double time, Type type, float x, float y) : time(time), type(type), x(x), y(y) {}
        Event(double time, Type type, Ra::Bounds b) : time(time), type(type), bounds(b) {}
        Event(double time, Type type, unsigned short keyCode, const char *chars) : time(time), type(type), keyCode(keyCode) {}
        Event(double time, Type type, size_t flags) : time(time), type(type), flags(flags) {}
        
        double time;
        Type type;
        float x, y;
        Ra::Bounds bounds;
        int keyCode; 
        size_t flags;
    };
    
    bool writeEvent(Event e) {
        const KeyCode keyCodes[] = { kC, kF, kI, kO, kP, k0, k1, kL, kReturn };
        bool written = false;
        if (e.type == Event::kKeyDown) {
            for (int keyCode : keyCodes)
                if (e.keyCode == keyCode) {
                    written = true;
                    break;
                }
        } else if (e.type == Event::kMouseMove) {
            mx = e.x, my = e.y;
            if (flags & Event::kShift)
                timeScale = powf(e.y / (bounds.uy - bounds.ly), 2.0);
            written = mouseMove;
        } else
            written = true;
        if (written)
            events.emplace_back(e);
        return written;
    }
    void readEvents(Ra::SceneList& list) {
        for (Event& e : events) {
            switch(e.type) {
                case Event::kMouseMove:
                    break;
                case Event::kMouseUp:
                    mouseDown = false;
                    break;
                case Event::kMouseDown:
                    mouseDown = true;
                    break;
                case Event::kFlags:
                    flags = e.flags;
                    break;
                case Event::kKeyDown:
                    if (e.keyCode == KeyCode::k1)
                        animating = !animating;
                    else if (e.keyCode == KeyCode::k0)
                        clock = 0.0;
                    else if (e.keyCode == KeyCode::kC)
                        useCurves = !useCurves;
                    else if (e.keyCode == KeyCode::kF)
                        ;
                    else if (e.keyCode == KeyCode::kI)
                        opaque = !opaque;
                    else if (e.keyCode == KeyCode::kO)
                        outlineWidth = outlineWidth ? 0.f : -1.f;
                    else if (e.keyCode == KeyCode::kP)
                        mouseMove = !mouseMove, indices = mouseMove ? indices : Ra::Range(INT_MAX, INT_MAX);
                    else if (e.keyCode == KeyCode::kL) {
                        locked = locked.begin != INT_MAX ? Ra::Range(INT_MAX, INT_MAX) : indices;
                        if (locked.begin != INT_MAX) {
                            Ra::Geometry *p = list.scenes[locked.begin].paths->base[locked.end].ptr;
                            p = p;
                        }
                    }
                    break;
                case Event::kKeyUp:
                    break;
                case Event::kMagnify:
                    if ((flags & Event::kShift) == 0)
                        magnify(e.x);
                    else
                        magnify(e.x, mx, my);
                    break;
                case Event::kRotate:
                    if ((flags & Event::kShift) == 0)
                        rotate(e.x);
                    else
                        rotate(e.x, mx, my);
                    break;
                case Event::kDragged:
                    translate(e.x, e.y);
                    mx += e.x, my += e.y;
                    break;
                case Event::kTranslate:
                    translate(e.x, e.y);
                    break;
                case Event::kFit: {
                    fit(e.bounds);
                    break;
                }
                case Event::kNull:
                    break;
            }
        }
        events.resize(0);
        if (mouseMove)
            indices = RasterizerWinding::indicesForPoint(list, getView(), getDevice(), scale * mx, scale * my);
        if (animating)
            clock += timeScale / 60.0;
    }
    
    bool needsRedraw() {  return animating || events.size() > 0;  }
    
    bool mouseDown = false, mouseMove = false, useCurves = true, animating = false, opaque = false;
    double clock = 0.0, timeScale = 0.333;
    float mx, my, outlineWidth = 0.f;
    Ra::Range indices = Ra::Range(INT_MAX, INT_MAX), locked = Ra::Range(INT_MAX, INT_MAX);
    size_t flags = 0;
    std::vector<Event> events;
};

typedef RasterizerState RaSt;
