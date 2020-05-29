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
    enum KeyCode { kC = 8, kL = 37, kO = 31, kP = 35, k1 = 18, k0 = 29, kReturn = 36 };
    struct Event {
        enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
        enum Type { kNull = 0, kMouseMove, kMouseUp, kDragged, kMouseDown, kFlags, kKeyDown, kKeyUp, kMagnify, kRotate, kTranslate, kFit };
        
        Event() {}
        Event(double time, Type type, float x, float y) : time(time), type(type), x(x), y(y) {}
        Event(double time, Type type, Ra::Bounds b) : time(time), type(type), bounds(b) {}
        Event(double time, Type type, unsigned short keyCode) : time(time), type(type), keyCode(keyCode) {}
        Event(double time, Type type, size_t flags) : time(time), type(type), flags(flags) {}
        
        double time;
        Type type;
        float x, y;
        Ra::Bounds bounds;
        int keyCode;
        size_t flags;
    };
    
    bool writeEvent(Event e) {
        const KeyCode keyCodes[] = { kC, kO, kP, k0, k1, kL, kReturn };
        bool written = e.type != Event::kKeyDown;
        if (e.type == Event::kKeyDown)
            for (int keyCode : keyCodes)
                if (e.keyCode == keyCode) {
                    written = true;
                    break;
                }
        if (written)
            events.emplace_back(e);
        return written;
    }
    void readEvents(Ra::SceneList& list) {
        for (Event& e : events) {
            switch(e.type) {
                case Event::kMouseMove:
                    x = e.x, y = e.y;
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
                    keyDown = true, keyCode = e.keyCode;
                    if (e.keyCode == KeyCode::k1)
                        animating = !animating;
                    else if (e.keyCode == KeyCode::kC)
                        useCurves = !useCurves;
                    else if (e.keyCode == KeyCode::kO)
                        outlineWidth = outlineWidth ? 0.f : -1.f;
                    else if (e.keyCode == KeyCode::kP)
                        mouseMove = !mouseMove, locked = Ra::Range(INT_MAX, INT_MAX), indices = mouseMove ? indices : Ra::Range(INT_MAX, INT_MAX);
                    else if (e.keyCode == KeyCode::kL && mouseMove)
                        locked = locked.begin != INT_MAX ? Ra::Range(INT_MAX, INT_MAX) : indices;
                    break;
                case Event::kKeyUp:
                    keyDown = true, keyCode = e.keyCode;
                    break;
                case Event::kMagnify:
                    magnify(e.x);
                    break;
                case Event::kRotate:
                    rotate(e.x);
                    break;
                case Event::kDragged:
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
        prepare();
        if (mouseMove)
            doMouseMove(list);
    }
    bool shouldRedraw() {  return animating || events.size() > 0;  }
    void resetEvents() {  events.resize(0);  }
    void doMouseMove(Ra::SceneList& list) {
        if (mouseMove && list.pathsCount)
            indices = RasterizerWinding::indicesForPoint(list, view, device, scale * x, scale * y);
    }
    bool keyDown = false, mouseDown = false, mouseMove = false, useCurves = true, animating = false;
    double time;
    float x, y;
    int keyCode = 0;
    Ra::Range indices = Ra::Range(INT_MAX, INT_MAX), locked = Ra::Range(INT_MAX, INT_MAX);
    size_t flags = 0;
    std::vector<Event> events;
    
    void update(float s, float w, float h) {
        scale = s, bounds = Ra::Bounds(0.f, 0.f, w, h), prepare();
    }
    void prepare() {
        view = Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(ctm);
        device = Ra::Bounds(0.f, 0.f, ceilf(scale * bounds.ux), ceilf(scale * bounds.uy));
    }
    void magnify(float s) {
        ctm = ctm.concat(Ra::Transform(s, 0.f, 0.f, s, 0.f, 0.f), 0.5f * (bounds.lx + bounds.ux), 0.5f * (bounds.ly + bounds.uy));
    }
    void rotate(float a) {
        ctm = ctm.concat(Ra::Transform::rst(a), 0.5f * (bounds.lx + bounds.ux), 0.5f * (bounds.ly + bounds.uy));
    }
    void translate(float x, float y) {
        ctm.tx += x, ctm.ty += y;
    }
    void fit(Ra::Bounds b) {
        Ra::Transform fit = bounds.fit(b);
        if (ctm.a == fit.a && ctm.b == fit.b && ctm.c == fit.c && ctm.d == fit.d && ctm.tx == fit.tx && ctm.ty == fit.ty)
            ctm = Ra::Transform();
        else
            ctm = fit;
    }
    
    float scale, outlineWidth = 0.f;
    Ra::Transform ctm, view;
    Ra::Bounds bounds, device;
};

typedef RasterizerState RaSt;
