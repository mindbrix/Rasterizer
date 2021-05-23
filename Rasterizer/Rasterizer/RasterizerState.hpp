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
    typedef void (*EventFunction)(Ra::SceneList& list, RasterizerState& state, void *info);
    typedef void (*WriteFunction)(Ra::SceneList& list, void *info);
    
    enum KeyCode { kC = 8, kF = 3, kI = 34, kL = 37, kO = 31, kP = 35, k1 = 18, k0 = 29, kReturn = 36 };
    struct Event {
        enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
        enum Type { kNull = 0, kMouseMove, kMouseUp, kDragged, kMouseDown, kFlags, kKeyDown, kKeyUp, kMagnify, kRotate, kTranslate, kFit };
        
        Event() {}
        Event(double time, Type type, float x, float y) : time(time), type(type), x(x), y(y) {}
        Event(double time, Type type, Ra::Bounds b) : time(time), type(type), bounds(b) {}
        Event(double time, Type type, unsigned short keyCode, const char *chars) : time(time), type(type), keyCode(keyCode) { characters = characters + chars; }
        Event(double time, Type type, size_t flags) : time(time), type(type), flags(flags) {}
        
        double time;
        Type type;
        float x, y;
        Ra::Bounds bounds;
        int keyCode;  Ra::Row<char> characters;
        size_t flags;
    };
    
    bool writeEvent(Event e) {
        const KeyCode keyCodes[] = { kC, kF, kI, kO, kP, k0, k1, kL, kReturn };
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
    void readEvents(Ra::SceneList& list,
                    EventFunction eventFunction, void *eventInfo,
                    WriteFunction writeFunction, void *writeInfo) {
        for (Event& e : events) {
            switch(e.type) {
                case Event::kMouseMove:
                    mx = e.x, my = e.y;
                    if (flags & Event::kShift)
                        timeScale = e.y / (bounds.uy - bounds.ly), timeScale *= timeScale;
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
                    else if (e.keyCode == KeyCode::k0)
                        clock = 0.0;
                    else if (e.keyCode == KeyCode::kC)
                        useCurves = !useCurves;
                    else if (e.keyCode == KeyCode::kF)
                        fastOutlines = !fastOutlines;
                    else if (e.keyCode == KeyCode::kI)
                        opaque = !opaque;
                    else if (e.keyCode == KeyCode::kO)
                        outlineWidth = outlineWidth ? 0.f : -1.f;
                    else if (e.keyCode == KeyCode::kP)
                        mouseMove = !mouseMove, indices = mouseMove ? indices : Ra::Range(INT_MAX, INT_MAX);
                    else if (e.keyCode == KeyCode::kL) {
                        locked = locked.begin != INT_MAX ? Ra::Range(INT_MAX, INT_MAX) : indices;
                        if (locked.begin != INT_MAX) {
                            Ra::Path& p = list.scenes[locked.begin].paths->base[locked.end];
                            p = p;
                        }
                    }
                    break;
                case Event::kKeyUp:
                    keyDown = false, keyCode = e.keyCode;
                    break;
                case Event::kMagnify:
                    if ((flags & Event::kShift) == 0)
                        magnify(e.x, 0.5f * (bounds.lx + bounds.ux), 0.5f * (bounds.ly + bounds.uy));
                    else
                        magnify(e.x, mx, my);
                    break;
                case Event::kRotate:
                    if ((flags & Event::kShift) == 0)
                        rotate(e.x, 0.5f * (bounds.lx + bounds.ux), 0.5f * (bounds.ly + bounds.uy));
                    else
                        rotate(e.x, mx, my);
                    break;
                case Event::kDragged:
                    if (eventFunction == NULL || (flags & Event::kShift))
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
        view = Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(ctm);
        if (eventFunction)
            (*eventFunction)(list, *this, eventInfo);
        events.resize(0);
        if (writeFunction)
            (*writeFunction)(list.empty(), writeInfo);
        if (mouseMove)
            indices = RasterizerWinding::indicesForPoint(list, view, device, scale * mx, scale * my);
        if (animating)
            clock += timeScale / 60.0;
    }
    bool needsRedraw() {  return animating || events.size() > 0;  }
    
    bool keyDown = false, mouseDown = false, mouseMove = false, useCurves = true, animating = false, opaque = false, fastOutlines = true;
    double clock = 0.0, timeScale = 0.333;
    float mx, my;
    int keyCode = 0;
    Ra::Range indices = Ra::Range(INT_MAX, INT_MAX), locked = Ra::Range(INT_MAX, INT_MAX);
    size_t flags = 0;
    std::vector<Event> events;
    
    void update(float s, float w, float h) {
        scale = s, bounds = Ra::Bounds(0.f, 0.f, w, h), device = Ra::Bounds(0.f, 0.f, ceilf(scale * w), ceilf(scale * h)), view = Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(ctm);
    }
    void magnify(float s, float ax, float ay) {
        ctm = ctm.preconcat(Ra::Transform(s, 0.f, 0.f, s, 0.f, 0.f), ax, ay);
    }
    void rotate(float a, float ax, float ay) {
        float sine, cosine;  __sincosf(a, & sine, & cosine);
        ctm = ctm.preconcat(Ra::Transform(cosine, sine, - sine, cosine, 0, 0), ax, ay);
    }
    void translate(float x, float y) {
        ctm.tx += x, ctm.ty += y;
    }
    void fit(Ra::Bounds b) {
        float sx = (bounds.ux - bounds.lx) / (b.ux - b.lx), sy = (bounds.uy - bounds.ly) / (b.uy - b.ly), s = sx < sy ? sx : sy;
        Ra::Transform fit = { s, 0.f, 0.f, s, -s * b.lx, -s * b.ly };
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
