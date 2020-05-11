//
//  RasterizerRenderer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 10/05/2020.
//  Copyright © 2020 @mindbrix. All rights reserved.
//

#import "RasterizerQueue.hpp"
#import "RasterizerState.hpp"

struct RasterizerRenderer {
     struct RenderContext {
         static const int kQueueCount = 8;
         void reset() { for (auto& ctx : contexts) ctx.reset(); }
         Ra::Context contexts[kQueueCount];
         RasterizerQueue queues[kQueueCount];
     };
     
     struct ThreadInfo {
         Ra::Context *context;
         Ra::SceneList *list;
         Ra::Transform view, *ctms, *clips;
         uint32_t *idxs;
         Ra::Colorant *colors;
         float *widths, outlineWidth;
         Ra::Bounds *bounds;
         Ra::Buffer *buffer;
         std::vector<Ra::Buffer::Entry> *entries;
         size_t begin;
         
         static void drawList(void *info) {
             ThreadInfo *ti = (ThreadInfo *)info;
             ti->context->drawList(*ti->list, ti->view, ti->idxs, ti->ctms, ti->colors, ti->clips, ti->widths, ti->bounds, ti->outlineWidth, ti->buffer);
         }
         static void writeContexts(void *info) {
             ThreadInfo *ti = (ThreadInfo *)info;
             Ra::writeContextToBuffer(*ti->list, ti->context, ti->idxs, ti->begin, *ti->entries, *ti->buffer);
         }
     };
     static void renderList(RenderContext& renderContext, Ra::SceneList& list, RasterizerState& state, Ra::Buffer *buffer) {
         if (list.pathsCount == 0)
             return;
         assert(sizeof(uint32_t) == sizeof(Ra::Colorant));
         uint32_t *idxs = (uint32_t *)malloc(list.pathsCount * sizeof(uint32_t));
         Ra::Transform *ctms = (Ra::Transform *)malloc(list.pathsCount * sizeof(state.view));
         Ra::Colorant *colors = (Ra::Colorant *)malloc(list.pathsCount * sizeof(Ra::Colorant));
         Ra::Transform *clips = (Ra::Transform *)malloc(list.pathsCount * sizeof(state.view));
         float *widths = (float *)malloc(list.pathsCount * sizeof(float));
         Ra::Bounds *bounds = (Ra::Bounds *)malloc(list.pathsCount * sizeof(Ra::Bounds));
         
         if (state.outlineWidth) {
             Ra::Colorant black(0, 0, 0, 255);
             memset_pattern4(colors, & black, list.pathsCount * sizeof(Ra::Colorant));
         } else {
             Ra::Scene *scene = & list.scenes[0];
             for (size_t i = 0, iz = 0; i < list.scenes.size(); i++, iz += scene->count, scene++)
                 memcpy(colors + iz, & scene->colors[0].src0, scene->count * sizeof(Ra::Colorant));
         }
         if (state.index != INT_MAX)
             colors[state.index].src0 = 0, colors[state.index].src1 = 0, colors[state.index].src2 = 255, colors[state.index].src3 = 255;
         
         renderListOnQueues(list, state, list.pathsCount, idxs, ctms, colors, clips, widths, bounds, state.outlineWidth, & renderContext.contexts[0], buffer, true, renderContext.queues);
         free(idxs), free(ctms), free(colors), free(clips), free(widths), free(bounds);
    }
    
    static void renderListOnQueues(Ra::SceneList& list, RasterizerState& state, size_t pathsCount, uint32_t *idxs, Ra::Transform *ctms, Ra::Colorant *colors, Ra::Transform *clips, float *widths, Ra::Bounds *bounds, float outlineWidth, Ra::Context *contexts, Ra::Buffer *buffer, bool multithread, RasterizerQueue *queues) {
        size_t eiz = 0, total = 0, count, divisions = RenderContext::kQueueCount, base, i, iz, izeds[divisions + 1], target, *izs = izeds;
        for (int j = 0; j < list.scenes.size(); j++)
            eiz += list.scenes[j].count, total += list.scenes[j].weight;
        if (buffer)
            buffer->useCurves = state.useCurves;
        ThreadInfo threadInfo[RenderContext::kQueueCount], *ti = threadInfo;
        if (multithread) {
            ti->context = contexts, ti->list = & list, ti->view = state.view, ti->idxs = idxs, ti->ctms = ctms, ti->clips = clips, ti->colors = colors, ti->widths = widths, ti->bounds = bounds, ti->outlineWidth = outlineWidth, ti->buffer = buffer;
            for (i = 1; i < RenderContext::kQueueCount; i++)
                threadInfo[i] = threadInfo[0], threadInfo[i].context += i;
            izeds[0] = 0, izeds[divisions] = eiz;
            auto scene = & list.scenes[0];
            for (count = base = iz = 0, i = 1; i < divisions; i++) {
                for (target = total * i / divisions; count < target; iz++) {
                    if (iz - base == scene->count)
                        scene++, base = iz;
                    count += scene->paths[iz - base].ref->types.size();
                }
                izeds[i] = iz;
            }
            count = RenderContext::kQueueCount;
            for (i = 0; i < count; i++)
                contexts[i].setGPU(state.device.ux, state.device.uy, pathsCount, izs[i], izs[i + 1]);
            RasterizerQueue::scheduleAndWait(queues, RenderContext::kQueueCount, ThreadInfo::drawList, threadInfo, sizeof(ThreadInfo), count);
        } else {
            count = 1;
            contexts[0].setGPU(state.device.ux, state.device.uy, pathsCount, 0, eiz);
            contexts[0].drawList(list, state.view, idxs, ctms, colors, clips, widths, bounds, outlineWidth, buffer);
        }
        std::vector<Ra::Buffer::Entry> entries[count];
        size_t begins[count], size = Ra::writeContextsToBuffer(list, contexts, count, colors, ctms, clips, widths, bounds, eiz, begins, *buffer);
        if (count == 1)
            Ra::writeContextToBuffer(list, contexts, idxs, begins[0], entries[0], *buffer);
        else {
            for (i = 0; i < count; i++)
                threadInfo[i].begin = begins[i], threadInfo[i].entries = & entries[i];
            RasterizerQueue::scheduleAndWait(queues, RenderContext::kQueueCount, ThreadInfo::writeContexts, threadInfo, sizeof(ThreadInfo), count);
        }
        for (int i = 0; i < count; i++)
            for (auto entry : entries[i])
                *(buffer->entries.alloc(1)) = entry;
        size_t end = buffer->entries.end == 0 ? 0 : buffer->entries.base[buffer->entries.end - 1].end;
        assert(size >= end);
    }
 };

 typedef RasterizerRenderer RaR;
