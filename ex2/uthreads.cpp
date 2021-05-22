#include "uthreads.h"
#include <deque>
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <algorithm>
#include <string>

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
        "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#endif

typedef ErrorType enum ErrorType
{
    system,
    threadLibrary
};

typedef ReturnValue enum ReturnValue
{
    failure = -1,
    success = 0
};

typedef ErrorMessages enum ErrorMessages
{
    InvalidInput = "invalid input",
    MutexError = "mutex error",
    ThreadDoesNotExist = "thread doesn't exist",
    ReachedMaxThreadNum = "reached maximum number of threads",
    MemoryAllocFailed = "memory allocation failed",
    TimerError = "timer error",
    MainThreadBlock = "attempting to block the main thread",
    MaskingFailed = "masking failed"
};

/*
 * Thread state enum
 */
typedef ThreadState enum ThreadState
{
    ready,
    blocked,
    mutex_block,
    running
};

/*
 * Mutex state enum
 */
typedef MutexState enum MutexState
{
    locked,
    free
};

/*
 * Thread struct representation
 */
struct Thread
{
    int ID;
    int quantum;
    State state;
    sigjmp_buf env; // an array keeping al the data of the thread
    char stack[STACK_SIZE];
} typedef Thread;

/*
 * Mutex struct representation
 */
struct Mutex
{
    int state;
    int running_thread_id;
    int *blocked_list;
    int num_of_blocked;
} typedef Mutex;

/*
 * Thread manager class
 */
class ThreadManager
{
private:
    int running_id = 0;
    int thread_count = 0;
    int quantum_usecs;
    int total_quantum = 0;
    deque<int> ready_threads;
    Mutex mutex = {MutexState::free, 0, nullptr};
    Thread *threads[MAX_THREAD_NUM]; //
    sigset_t mask;
    struct itimerval timer{}; // timer

public
    //Singleton
    static *ThreadManager manager;

    /**
     * @brief                   Singleton constructor
     * @param quantum_usecs     Thread max running time.
     */
    ThreadManager(int quantum_usecs) : threads(), ready_threads(), mask()
    {
        // todo need to add sigaddset, sigempty set,  SIGVTALRM,sa_hanlder
        this->quantum_usecs = quantum_usecs;

        for (int i = 0; i < MAX_THREAD_NUM; i++)
        {
            threads[i] = nullptr;
        }
        Thread main = {0, 1, running, nullptr};
        threads[0] = &main; // check for point fichs
        this->thread_count++;
        if (sigemptyset(&this->mask)) // inits mask
        {
            raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
            clean_exit();
        }
        if (sigaddset(&this->mask, SIGVTALRM)) // adds signal
        {
            raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
            clean_exit();
        }
        if (sigprocmask(SIG_SETMASK, &this->mask, nullptr) == -1) // sets the mask
        {
            raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
            clean_exit();
        }
        struct sigaction sa{};
        sa.sa_handler = &static_manage;
        if (sigaction(SIGVTALRM, &sa, nullptr) < 0)  // add action to the SIGVTALRM signal
        {
            raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
            clean_exit();
        }
        if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
        {
            raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
            clean_exit();
        }
    }

    /*
     * wraper for the manage ready function
     */
    static void static_manage(int sig) {
        (void)sig;
        manager->manage_ready();
    }

    /*
 * Raise error msg
 */
    void raise_error(ErrorType type, string errorMsg)
    {
        string startMsg = "%s";
        if (type == ErrorType::system)
        {
            startMsg = "system error: %s";
        }
        else if (type == ErrorType::threadLibrary)
        {
            startMsg = "thread library error: %s"
        }
        fprintf(stderr, startMsg, errorMsg);
    }

    /**
     * @brief       Init thread
     * @param id    Thread ID
     * @param f     Address
     * @return      0 if success, -1 otherwise
     */
    int init_thread(int id, void *f)
    {
        Thread newThread = {id, 0, ready, f};
        address_t sp, pc;
        sp = (address_t) stack1 + STACK_SIZE - sizeof(address_t);
        pc = (address_t) f;
        sigsetjmp(newThread->env, 1);
        (newThread->env->__jmpbuf)[JB_SP] = translate_address(sp);
        (newThread->env->__jmpbuf)[JB_PC] = translate_address(pc);
        if (sigemptyset(&newThread->env->__saved_mask))
        {
            // error message
            exit(-1)
        }
        this->thread_count++;
        this->ready_threads.push(id);
        this->threads[id] = &newThread;
        return id;
    }

    /**
     * @brief   Get thread count
     * @return  Threads count
     */
    int get_thread_count()
    {
        return this->thread_count;
    }

    /**
     * @brief   Get new thread ID
     * @return  New thread ID
     */
    int get_new_id()
    {
        for (int i = 0; i < MAX_THREAD_NUM; i++)
        {
            if (this->threads[i] == nullptr)
            {
                return i;
            }
        }
        return -1;
    }

    /**
     * @brief   Threads scheduler
     * @return  0 if success, -1 otherwise
     */
    int manage_ready()
    {
        //todo: moves the threads from ready to running
        // need to use siglongjmp
        // add sigprocmask checks
        if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1)
        {
            this->raise_error(ErrorType::system, ErrorMessages:: MaskingFailed);
            this->clean_exit();
        }
        if (sigsetjmp(this->threads[this->running_id]->env, 1)) // saves the current thread env
        {
           if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) {
               this->raise_error(ErrorType::system, ErrorMessages::MaskingFailed);
               this->clean_exit();
           }
           return; // todo check forum
        }
        if (this->threads[this->running_id]->state == running) // if running is blocked
        {
            this->ready_threads.push(this->running_id);
            this->threads[this->running_id]->state = ready;
        }
        this->running_id = this->get_next_thread();
        this->threads[this->running_id]->quantum++;
        this->total_quantum++;
        this->threads[this->running_id]->state = running;
        if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) {
            this->raise_error(ErrorType::system, ErrorMessages::MaskingFailed);
            this->clean_exit();
        }
        this->time();
        siglongjmp(this->threads[this->running_id]->env, 1);
    }

    int get_next_thread()
    {
        id = this->ready_threads.popfront();
        while((this->threads[id]->state == ThreadState::ready))
        {
            if (this->threads[id]->state == ThreadState::mutex_block)
            {
                if (this->lock_mutex(id) == 0)
                {
                    return id;
                }
            }
            this->ready_threads.push(id);
            id = this->ready_threads.popfront();
        }
        return id;
    }

    /**
     * @brief       Validate thread ID
     * @param id    ID
     * @return      0 if success, -1 otherwise
     */
    bool valid_id(int id)
    {
        return this->threads[id] != nullptr && id <= MAX_THREAD_NUM && id >= 0;
    }

    bool id_exist(int id)
    {
        for (int i = 0; i < MAX_THREAD_NUM; i++)
        {
            if (this->threads[i] != nullptr && this->threads[i]->ID == id)
            {
                return true;
            }
        }
        return false;
    }

    int lock_mutex(int id)
    {
        if (this->mutex.state == MutexState::free)
        {
            mutex.state == MutexState ::locked;
            mutex.running_thread_id = tid;
            return 0;
        }
        return -1;
    }

    /**
     * @brief       Free thread by thread ID
     * @param id    Thread ID
     * @return      0 if success, -1 otherwise
     */
    int free_thread(int id)
    {
        free(this->threads[id])
        this->threads[id] = nullptr;
        this->thread_count--;
    }

    /**
     * @brief   Get all threads quantum
     * @return  All threads quantum
     */
    int get_quantum_usecs()
    {
        return this->total_quantum;
    }

    /**
     * @brief       Get thread quantum by thread ID
     * @param tid   Thread ID
     * @return      Thread quantum
     */
    int get_thread_quantum(int tid)
    {
        return this->threads[tid]->quantum;
    }

    /**
     * @brief   Get mutex reference
     * @return  Mutex
     */
    Mutex &get_mutex()
    {
        return this->m;
    }

    /**
     * @brief   Get running thread ID
     * @return  Running thread ID
     */
    int get_running_id()
    {
        return this->running_id;
    }

    /**
     * @brief       Block thread
     * @param tid   Thread ID
     * @return      0 if success, -1 otherwise
     */
    int block_thread(int tid)
    {
        if (tid == 0)
        {
            this->raise_error(ErrorType::threadLibrary, ErrorMessages::MainThreadBlock)
            return ReturnValue::failure;
        }
        if (this->threads[tid]->state == ThreadState::ready || this->threads[tid]->state == ThreadState::running) // allready blocked
        {
            this->threads[tid]->state = ThreadState::blocked;
            if (this->running_id == tid) // blocked thread is current running
            {
                return this->manage_ready();
            }
        }
        return ReturnValue::success;
    }

    /**
     * @brief       Resume thread
     * @param tid   Thread ID
     * @return      0 if success, -1 otherwise
     */
    int resume_thread(int tid)
    {
        if (this->threads[tid]->state == blocked)
        {
            this->threads[tid]->state = ready;
        }
        return ReturnValue::success;
    }

    int time()
    {
        timer.it_value.tv_sec = this->quantum_usecs / (int) 1e6;
        timer.it_value.tv_usec = this->quantum_usecs % (int) 1e6;
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
        if (setitimer(ITIMER_VIRTUAL, &timer, nullptr))
        {
            this->raise_error(ErrorType::system, ErrorMessages::TimerError);
            clean_exit();
            exit(1);
        }
    }

    int terminate(int tid)
    {
        if (tid == 0)
        {
            clean_exit();
        }
        if (this->mutex.running_thread_id == tid)
        {
            this->unlock_mutex();
        }
        if (this->running_id == tid)
        {
            this->manage_ready(); // check if okay, if not add state
        }
        this->ready_threads.erase(std::remove(this->ready_threads.begin(), this->ready_threads.end(), tid),
                this->ready_threads.end());
        this->free_thread(tid);
    }

    this unlock_mutex()
    {
        if (this->mutex.state == MutexState::free)
        {
            clean_exit();
        }
        this->mutex.state = MutexState ::free;
        this->mutex.running_thread_id = -1;
    }

    int clean_exit()
    {
        // todo free memory
        for (int i = 0; i < MAX_THREAD_NUM; i++)
        {
            this->threads[i] = nullptr;
        }
        free(this);
        exit(-1);
    }

};

// need to use singleton
ThreadManager manager;

/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * the length of a quantum in micro-seconds. It is an error to call this
 * function with non-positive quantum_usecs.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs)
{
    if (quantum_usecs <= 0)
    {
        manager.raise_error(ErrorType::threadLibrary, ErrorMessages::InvalidInput);
        return ReturnValue::failure;
    }
     ThreadManager::manager = new ThreadManager(quantum_usecs); // not sure if memory should be allocated
    if (!ThreadManager::manager)
    {
        ThreadManager::manager.raise_error(ErrorType::system, ErrorMessages::MemoryAllocFailed);
        exit(1);
    }
    return ReturnValue::success;
}

/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)(void))
{
    if (manager.get_thread_count() == MAX_THREAD_NUM)
    {
        manager.raise_error(ErrorType::threadLibrary, ErrorMessages::ReachedMaxThreadNum);
        return ReturnValue::failure;
    }
    if (!f)
    {
        manager.raise_error(ErrorType::threadLibrary, ErrorMessages::InvalidInput);
        return ReturnValue::failure;
    }
    if (sigprocmask(SIG_BLOCK, &this->mask, nullptr) == -1) // block the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    int id = manager.get_new_id();
    manager.init_thread(id, f);
    if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
}

/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid)
{
    if (!manager.id_exist(tid))
    {
        manager.raise_error(ErrorType::threadLibrary, ErrorMessages::ThreadDoesNotExist);
        return ReturnValue::failure;
    }
    if (sigprocmask(SIG_BLOCK, &this->mask, nullptr) == -1) // block the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    ThreadManager::manager.terminate(tid);
    if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    return ReturnValue::success;
}

/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid)
{
    if (!manager.id_exist(tid))
    {
        manager.raise_error(ErrorType::threadLibrary, ErrorMessages::ThreadDoesNotExist);
        return ReturnValue::failure;
    }
    if (sigprocmask(SIG_BLOCK, &this->mask, nullptr) == -1) // block the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    ThreadManager::manager.block_thread(tid);
    if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    return ReturnValue :: success;

}

/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state if it's not synced. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid)
{
    if (!manager.id_exist(tid))
    {
        manager.raise_error(ErrorType::threadLibrary, ErrorMessages::ThreadDoesNotExist);
        return ReturnValue::failure;
    }
    if (sigprocmask(SIG_BLOCK, &this->mask, nullptr) == -1) // block the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    ThreadManager::manager.resume_thread(tid);
    if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    return ReturnValue :: success;
}

/*
 * Description: This function tries to acquire a mutex.
 * If the mutex is unlocked, it locks it and returns.
 * If the mutex is already locked by different thread, the thread moves to BLOCK state.
 * In the future when this thread will be back to RUNNING state,
 * it will try again to acquire the mutex.
 * If the mutex is already locked by this thread, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_lock()
{
    if (sigprocmask(SIG_BLOCK, &this->mask, nullptr) == -1) // block the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    ThreadManager::manager.lock_mutex(ThreadManager::manager.get_running_id());
    if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    return ReturnValue :: success;
}

/*
 * Description: This function releases a mutex.
 * If there are blocked threads waiting for this mutex,
 * one of them (no matter which one) moves to READY state.
 * If the mutex is already unlocked, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_unlock()
{
    if (sigprocmask(SIG_BLOCK, &this->mask, nullptr) == -1) // block the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    ThreadManager::manager.unlock_mutex();
    if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
    {
        raise_error(ErrorType:: system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    return ReturnValue :: success;
}

/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid() {
    if (sigprocmask(SIG_BLOCK, &this->mask, nullptr) == -1) // block the singls
    {
        raise_error(ErrorType::system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    ThreadManager::manager.get_running_id();
    if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
    {
        raise_error(ErrorType::system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    return ReturnValue::success;
}

/*
 * Description: This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums()
{
    if (sigprocmask(SIG_BLOCK, &this->mask, nullptr) == -1) // block the singls
    {
        raise_error(ErrorType::system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    ThreadManager::manager.get_quantum_usecs();
    if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
    {
        raise_error(ErrorType::system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    return ReturnValue::success;
}

/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered an error.
 * Return value: On success, return the number of quantums of the thread with ID tid.
 * 			     On failure, return -1.
*/
int uthread_get_quantums(int tid)
{
    if (!manager.valid_id(tid))
    {
        raise_error(ErrorType::threadLibrary, ErrorMessages::InvalidInput);
        return ReturnValue::failure;
    }
    if (sigprocmask(SIG_BLOCK, &this->mask, nullptr) == -1) // block the singls
    {
        raise_error(ErrorType::system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    ThreadManager::manager.get_thread_quantum(tid);
    if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
    {
        raise_error(ErrorType::system, ErrorMessages::MaskingFailed);
        clean_exit();
    }
    return ReturnValue::success;
}