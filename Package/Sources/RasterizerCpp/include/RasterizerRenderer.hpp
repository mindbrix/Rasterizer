//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//
#import <CoreGraphics/CoreGraphics.h>


struct RasterizerRenderer {
    RasterizerRenderer() {
        size_t count = sysconf(_SC_NPROCESSORS_ONLN);
        contexts.resize(count < 1 ? 1 : count);
    }
    
    void renderList(const Ra::SceneList& list, float scale, float w, float h, Ra::Buffer *buffer) {
        size_t contextCount = contexts.size();
        buffer->prepare(list);
        
        Ra::Bounds device(0.f, 0.f, ceilf(scale * w), ceilf(scale * h));
        Ra::Transform view = list.ctm.concat(Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f));
        
        auto divisions = (size_t *)alloca((contextCount + 1) * sizeof(size_t));
        writeBalancedWeightDivisions(list, divisions);
        dispatch_apply(contextCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            contexts[i].drawList(list, device, view, divisions[i], divisions[i + 1], buffer);
        });
        auto begins = (size_t *)alloca(contextCount * sizeof(size_t));
        size_t size = Ra::resizeBuffer(list, & contexts[0], contextCount, begins, *buffer);
        dispatch_apply(contextCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            Ra::writeContextToBuffer(list, & contexts[0] + i, begins[i], i, contextCount, *buffer);
        });
        for (int i = 0; i < contextCount; i++)
            for (int j = 0; j < contexts[i].entries.end(); j++)
                *(buffer->entries.alloc(1)) = contexts[i].entries[j];
        size_t end = buffer->entries.end == 0 ? 0 : buffer->entries.back().end;
        assert(size >= end);
        
        auto colors = (Ra::Color *)(buffer->base + buffer->colors);
        colors[buffer->pathsCount] = buffer->params.clearColor;
    }
    
    void writeBalancedWeightDivisions(const Ra::SceneList& list, size_t *divisions) {
        size_t contextCount = contexts.size();
        size_t total = 0, count, si, i, iz, target;
        for (int j = 0; j < list.scenes.size(); j++)
            total += list.scenes[j]->weight();
        if (total == 0)
            memset(divisions, 0, (contextCount + 1) * sizeof(*divisions));
        else {
            divisions[0] = 0, divisions[contextCount] = list.pathsCount;
            auto scene = & list.scenes[0];
            for (count = si = iz = 0, i = 1; i < contextCount; i++) {
                for (target = total * i / contextCount; count < target; iz++, si++) {
                    if (si == (scene->ptr)->count())
                        scene++, si = 0;
                    count += (scene->ptr)->draws[si].path->types.end;
                }
                divisions[i] = iz;
            }
        }
    }
    void reset() {
        for (auto& ctx : contexts)
            ctx.reset();
    }
    
    std::vector<Ra::Context> contexts;
 };
