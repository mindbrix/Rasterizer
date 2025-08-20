//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for any purpose,
//  including commercial applications, and to alter it and redistribute it
//  freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//

struct RasterizerRenderer {
    
    void renderList(Ra::SceneList& list, float scale, float w, float h, Ra::Buffer *buffer) {
        Ra::Bounds device(0.f, 0.f, ceilf(scale * w), ceilf(scale * h));
        Ra::Transform view = Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(list.ctm);
        
        buffer->useCurves = list.useCurves;
        buffer->prepare(list);
         
        size_t divisions[kContextCount + 1], *pdivs = divisions;
        writeBalancedWeightDivisions(list, pdivs);
        dispatch_apply(kContextCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            contexts[i].drawList(list, device, view, pdivs[i], pdivs[i + 1], buffer);
        });
        size_t begins[kContextCount], *pbegins = begins, size;
        size = Ra::resizeBuffer(list, contexts, kContextCount, pbegins, *buffer);
        dispatch_apply(kContextCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            Ra::writeContextToBuffer(list, contexts + i, pbegins[i], *buffer);
        });
        for (int i = 0; i < kContextCount; i++)
            for (auto entry : contexts[i].entries)
                *(buffer->entries.alloc(1)) = entry;
        size_t end = buffer->entries.end == 0 ? 0 : buffer->entries.back().end;
        assert(size >= end);
    }
    
    void writeBalancedWeightDivisions(Ra::SceneList& list, size_t *divisions) {
        size_t total = 0, count, si, i, iz, target;
        for (int j = 0; j < list.scenes.size(); j++)
            total += list.scenes[j].weight;
        if (total == 0)
            memset(divisions, 0, (kContextCount + 1) * sizeof(*divisions));
        else {
            divisions[0] = 0, divisions[kContextCount] = list.pathsCount;
            auto scene = & list.scenes[0];
            for (count = si = iz = 0, i = 1; i < kContextCount; i++) {
                for (target = total * i / kContextCount; count < target; iz++, si++) {
                    if (si == scene->count)
                        scene++, si = 0;
                    count += scene->paths->base[si]->types.end;
                }
                divisions[i] = iz;
            }
        }
    }
    void reset() { for (auto& ctx : contexts) ctx.reset(); }
    
    static const int kContextCount = 8;
    Ra::Context contexts[kContextCount];
 };


 typedef RasterizerRenderer RaR;
