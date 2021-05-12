//
// Created by Weisl on 4/30/2021.
//
using namespace std;

#include <queue>
#include "uthreads.h"

typdef enum State{
    ready, blocked, running
}State ;

typedef enum MutexState
{
    locked, free
};
struct thread
{
    int ID;
    int quantum;
    State state;
    sigjmp_buf env;
    char stack[STACK_SIZE];
}typedef thread ;

struct mutex
{
    int state;
    int id_of_running;
    int list_of_blocked;
    int num_of_blocked;
}typedef mutex ;

class ThreadManager
{
private:
    int ID = 0;
    int thread_count = 0;
    int quantum_usecs = 0;
    int stack = 0; // add a stack
    thread* threads[MAX_THREAD_NUM];
    queue<thread> ready_threads;
    mutex m = {free, 0, nullptr};

public:
    int init_thread(int id, void* f)
    {
        thread th = {id, this->quantum_usecs, ready,f};
        this->thread_count++;
        ready_threads.push(th);
        this->threads[id] = &th;
        return id;
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
    int manage_id()
    {
        for (int i = 0; i<MAX_THREAD_NUM; i++)
        {
            if (this->threads[i] == nullptr)
            {
                return i;
            }
        }
        return -1;
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

    int get_quantum_usecs()
    {
        return this->quantum_usecs;
    }

    int get_thread_quantum(int tid)
    {
        return this->threads[tid]->quantum;
    }

    mutex& get_mutex()
    {
        return this->m;
    }

    int get_running_id()
    {
        return this->ID;
    }

    int set_to_block(int tid)
    {
        this->threads[tid]->state = blocked;
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
    if (!f)
    {
        // error message
        return -1;
    }
    int id = t.manage_id();
    if (id == -1)
    {
        return -1;
        // error messgae
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
    if (t.manage_id(tid) == -1)
    {
        return -1 // add error message
    }
    t.set_to_block(tid)

}

int uthread_resume(int tid)
{
    if (t.manage_id(tid) == -1)
    {
        return -1 // add error message
    }

}


int uthread_mutex_lock()
{
    if (t.get_mutex().state == free)
    {
        t.get_mutex().state == free;
        t.get_mutex().id_of_running = t.get_running_id();
        return 0;
    }
    if (t.get_mutex().state == locked)
    {
        if (t.get_mutex().id_of_running == t.get_running_id()) {
            // error message
            return -1;
        }
        t.get_mutex().list_of_blocked[t.get_mutex().num_of_blocked] = t.get_running_id();
        t.set_to_block(t.get_running_id());
    }
}


int uthread_mutex_unlock()
{
    if (t.get_mutex().state == free)
    {
        // error message
        return -1;
    }

}


int uthread_get_tid()
{
    return t.get_running_id()
}


int uthread_get_total_quantums()
{
    return t.get_quantum_usecs()

}

int uthread_get_quantums(int tid)
{
    if (t.check_id(tid) == -1)
    {
        return -1;
        // error message
    }
    return t.get_thread_quantum(tid);
}