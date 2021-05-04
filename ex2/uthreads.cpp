//
// Created by Weisl on 4/30/2021.
//
using namespace std;

#include <queue>
#include "uthreads.h"

enum State{
    ready, blocked, run
};

struct thread
{
    int ID;
    int quantum;
    State state;
    char stack[STACK_SIZE];
}typedef thread ;


class ThreadManager
{
private:
    int ID = 0;
    int thread_count = 0;
    int quantum_usecs = 0;
    int stack = 0; // add a stack
    queue<thread> ready_threads;
    thread* running = nullptr;

public:
    int init_thread()
    {
        thread t = {ID, this->quantum_usecs, ready};
        ID++;
        thread_count++;
        ready_threads.push(t);
        return t.ID;
    }

    void init_first(int quantum_usec)
    {
        this->quantum_usecs = quantum_usec;
    }

    int get_num()
    {
        return this->thread_count;
    }

    // sets the next availabe id
    int manage_id()
    {

    }

    // moves the threads from ready to running
    int manage_ready()
    {

    }
};

ThreadManager t;

int uthread_init(int quantum_usecs)
{
    if (quantum_usecs < 0)
    {
        return -1;
    }
    t.init_first(quantum_usecs);
    t.init_thread(); // inits the main tread
    return 0;
}

int uthread_spawn(void (*f)(void))
{
    if (t.get_num() == MAX_THREAD_NUM)
    {
        return -1;
    }
    t.init_thread(); // adds a new thread

}

int uthread_terminate(int tid)
{

}

int uthread_block(int tid)
{

}

int uthread_resume(int tid)
{

}


int uthread_mutex_lock()
{

}



int uthread_mutex_unlock()
{

}


int uthread_get_tid()
{

}


int uthread_get_total_quantums()
{

}



int uthread_get_quantums(int tid)
{

}