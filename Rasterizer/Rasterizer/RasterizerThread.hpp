//
//  RasterizerThread.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/06/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <vector>
#import <pthread.h>
#define NUM_THREADS 5

struct RasterizerThread {
    typedef void (*Function)(void *info);
    
    struct Arguments {
        Arguments() {}
        Arguments(Function function, void *info) : function(function), info(info) {}
        Function function = nullptr;
        void *info = nullptr;
    };

    struct Queue {
        static void *queue_main(void *arguments) {
            Queue *queue = (Queue *)arguments;
            while (1) {
                Arguments args = queue->get();
                (*args.function)(args.info);
                queue->pop();
            }
            return 0;
        }
        Queue() {
            pthread_create(& thread, NULL, queue_main, (void *)this);
            pthread_mutex_init(& mtx, NULL);
            pthread_cond_init(& added, NULL);
            pthread_cond_init(& removed, NULL);
        }
        ~Queue() {
            pthread_mutex_lock(& mtx);
            pthread_cancel(thread);
            pthread_mutex_destroy(& mtx);
            pthread_cond_destroy(& added);
            pthread_cond_destroy(& removed);
           // pthread_join(thread, NULL);
        }
        void add(Function function, void *info) {
            pthread_mutex_lock(& mtx);
            
            //printf("IN MAIN: add().\n");
            
            /* Add element normally. */
            arguments.emplace_back(function, info);
            
            pthread_mutex_unlock(& mtx);
            
            /* Signal waiting threads. */
            pthread_cond_signal(& added);
        }
        Arguments get() {
            pthread_mutex_lock(& mtx);
            
            //printf("IN QUEUE: get().\n");
            
            /* Wait for element to become available. */
            while (arguments.size() == 0)
                pthread_cond_wait(& added, & mtx);
            //printf("IN QUEUE::get() - size = %ld.\n", arguments.size());
            /* We have an element. Pop it normally and return it in val_r. */
            Arguments args = arguments[0];
        
            //printf("IN QUEUE::get() - size = %ld.\n", arguments.size());
            
            pthread_mutex_unlock(& mtx);
            return args;
        }
        void pop() {
            pthread_mutex_lock(& mtx);
            for (int i = 0; i < arguments.size() - 1; i++)
                arguments[i] = arguments[i + 1];
            arguments.resize(arguments.size() - 1);
            pthread_mutex_unlock(& mtx);
            pthread_cond_signal(& removed);
        }
        void foreach(Function function, void *arguments, size_t count, size_t stride) {
            uint8_t *base = (uint8_t *)arguments;
            for (int i = 0; i < count; i++, base += stride)
                add(function, (void *)base);
            
            //printf("IN MAIN: All threads are created.\n");
            wait();
            //printf("MAIN program has ended.\n");
        }
        
        void wait() {
            pthread_mutex_lock(& mtx);
            while (arguments.size())
                pthread_cond_wait(& removed, & mtx);
            pthread_mutex_unlock(& mtx);
        }
        pthread_t thread;
        pthread_mutex_t mtx;
        pthread_cond_t added, removed;
        std::vector<Arguments> arguments;
    };
    
    
    
    static void *perform_work(void *arguments) {
        Arguments *args = (Arguments *)arguments;
        (*args->function)(args->info);
        return 0;
    }
    
    static void foreach(Function function, void *arguments, size_t count, size_t stride) {
        pthread_t threads[count];
        Arguments args[count];
        int i;
        int result_code;
        Queue queue;
        
        uint8_t *base = (uint8_t *)arguments;
        for (i = 0; i < count; i++, base += stride)
            queue.add(function, (void *)base);
        
        printf("IN MAIN: All threads are created.\n");
        queue.wait();
        /*
        for (i = 0; i < count; i++, base += stride)
            args[i] = Arguments(function, (void *)base);
            
        //create all threads one by one
        for (i = 0; i < count; i++) {
            //            printf("IN MAIN: Creating thread %d.\n", i);
            result_code = pthread_create(& threads[i], NULL, perform_work, & args[i]);
            assert(!result_code);
        }
        
        
        
        
        //wait for each thread to complete
        for (i = 0; i < count; i++) {
            result_code = pthread_join(threads[i], NULL);
            assert(!result_code);
            //            printf("IN MAIN: Thread %d has ended.\n", i);
        }
        */
        
        printf("MAIN program has ended.\n");
    }
};
