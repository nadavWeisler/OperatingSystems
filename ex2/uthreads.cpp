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
    thread* threads[MAX_THREAD_NUM];
    queue<thread> ready_threads;
    thread* running = nullptr;

public:
    int init_thread()
    {
        thread t = {ID, this->quantum_usecs, ready};
        this->manage_id();
        thread_count++;
        ready_threads.push(t);
        this->threads[t.ID] = &t;
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

    // sets the next id
    void manage_id()
    {
        int id;
        for (int i = 0; i<MAX_THREAD_NUM; i++)
        {
            if (this->threads[i] == nullptr)
            {
                this->ID = i;
                break;
            }
        }
    }

    // moves the threads from ready to running
    int manage_ready()
    {

    }

    int check_id(int id)
    {
        if (this->threads[id] == nullptr || id > MAX_THREAD_NUM || id < 0)
        {
            return -1;
        }
        return 1;
    }

    int free_thread(int id)
    {
        this->threads[id] = nullptr;
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
    t.manage_ready();
    return 0;
}

int uthread_terminate(int tid)
{
    if (t.check_id(tid) == -1)
    {
        return -1;
    }
    t.free_thread(tid);
    return 0;

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