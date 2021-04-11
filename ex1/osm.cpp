//
// Created by weisler on 06/04/2021.
//

#include <iostream>
#include "osm.h"
#include <sys/time.h>

/**
 * An empty function
 */
void empty_func() {

}

/**
 * get the nano second representation
 * @param t
 * @return
 */
long get_nano(struct timeval t) {
    return ((t.tv_sec * 1000000 + t.tv_usec) * 1000);
}

/* Time measurement function for a simple arithmetic operation.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_operation_time(unsigned int iterations) {
    if(iterations == 0) {
        return 0;
    }
    timeval start_time;
    int a = 0, b = 1;
    gettimeofday(&start_time, nullptr);
    for (int i = 0; i < (int) iterations; i++) {
        a += b;
        a += b;
        a += b;
        a += b;
        a += b;
        a += b;
        a += b;
        a += b;
        a += b;
        a += b;
        a += b;
    }
    timeval end_time;
    gettimeofday(&end_time, nullptr);
    return (double) ((get_nano(end_time)) - get_nano(start_time)) / (iterations * 11);

}


/* Time measurement function for an empty function call.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_function_time(unsigned int iterations) {
    if(iterations == 0) {
        return 0;
    }
    timeval start_time;
    gettimeofday(&start_time, nullptr);
    for (int i = 0; i < (int) iterations; i++) {
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
        empty_func();
    }
    timeval end_time;
    gettimeofday(&end_time, nullptr);
    return (double) ((get_nano(end_time)) - get_nano(start_time)) / (iterations * 11);
}


/* Time measurement function for an empty trap into the operating system.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_syscall_time(unsigned int iterations) {
    if(iterations == 0) {
        return 0;
    }
    timeval start_time;
    gettimeofday(&start_time, nullptr);
    for (int i = 0; i < (int) iterations; i++) {
        OSM_nullptrSYSCALL;
        OSM_nullptrSYSCALL;
        OSM_nullptrSYSCALL;
        OSM_nullptrSYSCALL;
        OSM_nullptrSYSCALL;
        OSM_nullptrSYSCALL;
        OSM_nullptrSYSCALL;
        OSM_nullptrSYSCALL;
        OSM_nullptrSYSCALL;
        OSM_nullptrSYSCALL;
        OSM_nullptrSYSCALL;
    }
    timeval end_time;
    gettimeofday(&end_time, nullptr);
    return (double) ((get_nano(end_time)) - get_nano(start_time)) / (iterations * 11);
}


int main() {
    int iterations = 100000;
    std::cout << "System calls time: " << osm_syscall_time(iterations)
              << std::endl << "Functions time: " << osm_function_time(iterations)
              << std::endl << "Operations time: " << osm_operation_time(iterations) << std::endl;
}