//
//  RasterizerRenderer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 10/05/2020.
//  Copyright © 2020 @mindbrix. All rights reserved.
//


struct RasterizerRenderer {
    void renderList(Ra::SceneList& list, Ra::Bounds device, Ra::Bounds deviceClip, Ra::Transform view, Ra::Buffer *buffer) {
        assert(sizeof(uint32_t) == sizeof(Ra::Colorant));
        if (list.pathsCount == 0)
            return;
         buffer->prepare(list);
         buffer->device = device;
         buffer->clip = deviceClip;
         
        size_t divisions[kContextCount + 1], *pdivs = divisions;
        writeBalancedWeightDivisions(list, pdivs);
        dispatch_apply(kContextCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            contexts[i].drawList(list, device, deviceClip, view, pdivs[i], pdivs[i + 1], buffer);
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
    void reset() { for (auto& ctx : contexts) ctx.reset(); }
    
    static const int kContextCount = 8;
    Ra::Context contexts[kContextCount];
 };


 typedef RasterizerRenderer RaR;
