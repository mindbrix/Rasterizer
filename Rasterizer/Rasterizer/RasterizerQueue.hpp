//
//  RasterizerQueue.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/06/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <vector>
#import <pthread.h>

struct RasterizerQueue {
    RasterizerQueue() {
        pthread_mutex_init(& mtx, NULL);
        pthread_cond_init(& notempty, NULL);
        pthread_cond_init(& empty, NULL);
        pthread_create(& thread, NULL, queue_main, (void *)this);
    }
    ~RasterizerQueue() {
        pthread_mutex_lock(& mtx);
        pthread_cancel(thread);
        pthread_mutex_destroy(& mtx);
        pthread_cond_destroy(& notempty);
        pthread_cond_destroy(& empty);
    }
    typedef void (*Function)(void *info);
    static void scheduleAndWait(RasterizerQueue *queues, size_t count, Function function, void *info, size_t infostride) {
        if (queues == nullptr || count == 0 || function == nullptr || info == nullptr || infostride == 0)
            return;
        for (int i = 0; i < count; i++)
            queues[i].add(function, (void *)((uint8_t *)info + i * infostride));
        for (int i = 0; i < count; i++)
            queues[i].wait();
    }
private:
    struct Call {
        Call() {}
        Call(Function function, void *info) : function(function), info(info) {}
        Function function;
        void *info;
    };
    static void *queue_main(void *args) {
        while (1)
            ((RasterizerQueue *)args)->cycle();
        return 0;
    }
    void add(Function function, void *info) {
        pthread_mutex_lock(& mtx);
        if (calls.size() == 0)
            pthread_cond_signal(& notempty);
        calls.emplace_back(function, info);
        pthread_mutex_unlock(& mtx);
    }
    void cycle() {
        pthread_mutex_lock(& mtx);
        if (calls.size() == 0)
            pthread_cond_wait(& notempty, & mtx);
        Call call = calls[0];
        pthread_mutex_unlock(& mtx);
        (*call.function)(call.info);
        pthread_mutex_lock(& mtx);
        calls.erase(calls.begin());
        if (calls.size() == 0)
            pthread_cond_signal(& empty);
        pthread_mutex_unlock(& mtx);
    }
    void wait() {
        pthread_mutex_lock(& mtx);
        if (calls.size())
            pthread_cond_wait(& empty, & mtx);
        pthread_mutex_unlock(& mtx);
    }
    pthread_t thread;
    pthread_mutex_t mtx;
    pthread_cond_t notempty, empty;
    std::vector<Call> calls;
};
