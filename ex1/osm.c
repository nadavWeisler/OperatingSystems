//
// Created by Weisl on 4/6/2021.
//

#ifndef _OSM_H
#define _OSM_H

#include <sys/time.h>
#include <stdio.h>
#include "osm.h"
/* calling a system call that does nothing */
#define OSM_NULLSYSCALL asm volatile( "int $0x80 " : : \
        "a" (0xffffffff) /* no such syscall */, "b" (0), "c" (0), "d" (0) /*:\
        "eax", "ebx", "ecx", "edx"*/)

/**
 * An empty function
 */
void empty_func()
{

}

/**
 * get the nano second representation
 * @param t
 * @return
 */
long get_nano(struct timeval t)
{
    return ((t.tv_sec * 1000000 + t.tv_usec) * 1000);
}

/* Time measurement function for a simple arithmetic operation.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */

double osm_operation_time(unsigned int iterations) {
    struct timeval current_time;
    struct timeval after_time;
    int a = 0, b = 1;
    gettimeofday(&current_time, NULL);
    for (int i = 0; i < iterations;i++)
    {
        a += b;
    }
    gettimeofday(&after_time, NULL);
    return (double)(get_nano(current_time) - get_nano(after_time))/ (iterations);

}


/* Time measurement function for an empty function call.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_function_time(unsigned int iterations)
{
    struct timeval current_time;
    struct timeval after_time;
    gettimeofday(&current_time, NULL);
    for (int i = 0; i < iterations;i++)
    {
        empty_func();
    }
    gettimeofday(&after_time, NULL);
    return (double)(get_nano(current_time) - get_nano(after_time))/ (iterations);
}

/* Time measurement function for an empty trap into the operating system.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_syscall_time(unsigned int iterations) {
    struct timeval current_time;
    struct timeval after_time;
    gettimeofday(&current_time, NULL);
    for (int i = 0; i < iterations; i++) {
        OSM_NULLSYSCALL;
    }
    gettimeofday(&after_time, NULL);
    return (double) (get_nano(current_time) - get_nano(after_time)) / (iterations);
}

int main()
{
    int iter = 1000;
    //int main(){
//    int iterations = 100000;
//    std::cout<<"System calls time "<<osm_syscall_time(iterations)
//    <<std::endl<<"Functions time"<<osm_function_time(iterations)
//    <<std::endl<<"Operations time"<<osm_operation_time(iterations)<<std::endl;
//}
}
#endif