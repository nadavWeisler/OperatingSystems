#include "uthreads.h"
#include <deque>
#include <cstdio>
#include <csetjmp>
#include <csignal>
#include <algorithm>
#include <string>
#include <sys/time.h>

using namespace std;

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr) {
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

/*
 * Error type enum
 */
enum ErrorType {
    systemError,
    threadLibrary
} typedef ErrorType;

void raise_error(ErrorType type, const char *errorMsg) {
    const char *startMsg = "%s";
    if (type == ErrorType::systemError) {
        startMsg = "systemError error: %s";
    } else if (type == ErrorType::threadLibrary) {
        startMsg = "thread library error: %s";
    }
    fprintf(stderr, startMsg, errorMsg);
}

/*
 * Return value enum
 */
enum ReturnValue {
    failure = -1,
    success = 0
} typedef ReturnValue;

/*
 * Thread state enum
 */
enum ThreadState {
    ready,
    blocked,
    running,
    terminated
} typedef ThreadState;

/*
 * Mutex state enum
 */
enum MutexState {
    locked,
    unlocked
} typedef MutexState;

/*
 * Thread struct representation
 */
struct Thread {
    int ID;
    int quantum;
    ThreadState state;
    sigjmp_buf env; // an array keeping al the data of the thread
    char stack[STACK_SIZE]{};
    bool blocked_by_mutex;

    /**
     * Struct constructor
     * @param ID    Thread ID
     * @param f     Memory address
     */
    Thread(int ID, void (*f)()) : ID(ID), quantum(0), state(ThreadState::ready), env(), blocked_by_mutex(false) {
        if (ID == 0) // main thread
        {
            return;
        }
        address_t sp, pc;
        sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
        pc = (address_t) f;
        sigsetjmp(env, 1);
        (env->__jmpbuf)[JB_SP] = translate_address(sp);
        (env->__jmpbuf)[JB_PC] = translate_address(pc);
        if (sigemptyset(&env->__saved_mask)) {
            fprintf(stderr, "systemError error: %s", "blocking failure");
            exit(-1);
        }
    }
} typedef Thread;

/*
 * Mutex struct representation
 */
struct Mutex {
    MutexState state;
    int locking_thread_id;
    deque<int> block_threads;

    Mutex() : state(MutexState::unlocked), locking_thread_id(-1), block_threads() {}
} typedef Mutex;

/*
 * Thread manager class
 */
class ThreadManager {
private:
    int running_id;
    int thread_count;
    int quantum_usecs;
    int total_quantum;
    std::deque<int> ready_threads;
    Mutex mutex;
    Thread *threads[MAX_THREAD_NUM]; //
    sigset_t mask;
    struct itimerval timer{}; // timer

public:
    //Singleton
    static ThreadManager *manager;

    /**
     * @brief                   Singleton constructor
     * @param quantum_usecs     Thread max running time.
     */
    explicit ThreadManager(int quantum_usecs) : running_id(0), thread_count(1),
                                                quantum_usecs(quantum_usecs), total_quantum(0), ready_threads(),
                                                mutex(), threads(), mask() {


        for (auto &thread : threads) {
            thread = nullptr;
        }
        auto *main = new Thread(0, nullptr);
        main->state = ThreadState::running;
        threads[0] = main; // check for point fichs
        this->thread_count++;
        this->total_quantum++;
        main->quantum++;
        if (sigemptyset(&this->mask)) // inits mask
        {
            raise_error(ErrorType::systemError, "masking failed");
            clean_exit();
        }
        if (sigaddset(&this->mask, SIGVTALRM)) // adds signal
        {
            raise_error(ErrorType::systemError, "masking failed");
            clean_exit();
        }
        if (sigprocmask(SIG_SETMASK, &this->mask, nullptr) == -1) // sets the mask
        {
            raise_error(ErrorType::systemError, "masking failed");
            clean_exit();
        }
        struct sigaction sa{};
        sa.sa_handler = &static_manage;
        if (sigaction(SIGVTALRM, &sa, nullptr) < 0)  // add action to the SIGVTALRM signal
        {
            raise_error(ErrorType::systemError, "masking failed");
            clean_exit();
        }
        if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) // unblock the singls
        {
            raise_error(ErrorType::systemError, "masking failed");
            clean_exit();
        }
        this->time();
    }

    /*
     * wraper for the manage ready function
     */
    static void static_manage(int sig) {
        (void) sig;
        manager->manage_ready();
    }

    /**
     * @brief       Init thread
     * @param id    Thread ID
     * @param f     Address
     * @return      0 if success, -1 otherwise
     */
    int init_thread(int id, void (*f)()) {
        auto *newThread = new Thread(id, f);
        this->thread_count++;
        this->ready_threads.push_back(id);
        this->threads[id] = newThread;
        return id;
    }

    /**
     * @brief   Get thread count
     * @return  Threads count
     */
    int get_thread_count() const {
        return this->thread_count;
    }

    /**
     * @brief   Get new thread ID
     * @return  New thread ID
     */
    int get_new_id() {
        for (int i = 0; i < MAX_THREAD_NUM; i++) {
            if (this->threads[i] == nullptr) {
                return i;
            }
        }
        return -1;
    }

    /**
     * @brief   Threads scheduler
     * @return  0 if success, -1 otherwise
     */
    void manage_ready() {
        if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) {
            raise_error(ErrorType::systemError, "masking failed");
            this->clean_exit();
        }
        if (this->threads[this->running_id]->state == ThreadState::running) // if running is blocked
        {
            this->ready_threads.push_back(this->running_id);
            this->threads[this->running_id]->state = ThreadState::ready;
        }

        if (sigsetjmp(this->threads[this->running_id]->env, 1)) // saves the current thread env
        {
            if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) {
                raise_error(ErrorType::systemError, "masking failed");
                this->clean_exit();
            }
            return; // todo check forum
        }
        if (this->threads[this->running_id]->state == ThreadState::terminated) {
            delete this->threads[this->running_id];
            this->threads[this->running_id] = nullptr;
        }
        this->running_id = this->get_next_thread();
        //this->running_id = this->ready_threads.front();
        //this->ready_threads.pop_front();
        this->threads[this->running_id]->quantum++;
        this->total_quantum++;
        this->threads[this->running_id]->state = running;
        if (sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) == -1) {
            raise_error(ErrorType::systemError, "masking failed");
            this->clean_exit();
        }
        this->time();
        siglongjmp(this->threads[this->running_id]->env, 1);
    }

    int get_next_thread() {
        int tid = this->ready_threads.front();
        this->ready_threads.pop_front();

        bool mutex_locked = this->threads[tid]->blocked_by_mutex;

        while (mutex_locked) {
            if (this->unlock_mutex(tid) == ReturnValue::failure) {
                this->ready_threads.push_back(tid);
                tid = this->ready_threads.front();
                this->ready_threads.pop_front();
                mutex_locked = this->threads[tid]->blocked_by_mutex;
            } else {
                mutex_locked = false;
            }
        }
        return tid;
    }

    /**
     * @brief       Validate thread ID
     * @param id    ID
     * @return      0 if success, -1 otherwise
     */
    bool valid_id(int id) {
        return this->threads[id] != nullptr && id <= MAX_THREAD_NUM && id >= 0;
    }

    bool id_exist(int id) {
        for (auto & thread : this->threads) {
            if (thread != nullptr && thread->ID == id) {
                return true;
            }
        }
        return false;
    }

    int lock_mutex(int tid) {
        if (this->mutex.locking_thread_id == tid) {
            return ReturnValue::failure; //todo  Wagarash cleanup
        }
        if (this->mutex.state == MutexState::unlocked) {
            this->mutex.state = MutexState::locked;
            this->mutex.locking_thread_id = tid;
        } else {
            this->threads[tid]->blocked_by_mutex = true;
            this->mutex.block_threads.push_back(tid);
            this->manage_ready();
        }
        return ReturnValue::success;
    }

    /*
     * Unlock mutex
     */
    int unlock_mutex(int tid) {
        if (this->mutex.state == MutexState::unlocked) {
            return ReturnValue::failure;
        }
        if (tid != this->mutex.locking_thread_id) {
            return ReturnValue::failure;
        }
        this->mutex.state = MutexState::unlocked;
        this->mutex.locking_thread_id = -1;
        if (!this->mutex.block_threads.empty()) {
            int new_tid = this->mutex.block_threads.front();
            this->mutex.block_threads.pop_front();
            this->mutex.locking_thread_id = new_tid;
            this->threads[new_tid]->blocked_by_mutex = false;
            this->mutex.state = MutexState::locked;
        }
        return ReturnValue::success;
    }


    /**
     * @brief       Free thread by thread ID
     * @param id    Thread ID
     * @return      0 if success, -1 otherwise
     */
    void free_thread(int id) {
        delete this->threads[id];
        this->threads[id] = nullptr;
        this->thread_count--;
    }

    /**
     * @brief   Get all threads quantum
     * @return  All threads quantum
     */
    int get_quantum_usecs() const {
        return this->total_quantum;
    }

    /**
     * @brief       Get thread quantum by thread ID
     * @param tid   Thread ID
     * @return      Thread quantum
     */
    int get_thread_quantum(int tid) {
        return this->threads[tid]->quantum;
    }

    /**
     * @brief   Get running thread ID
     * @return  Running thread ID
     */
    int get_running_id() const {
        return this->running_id;
    }

    static bool is_main_thread(int tid) {
        return tid == 0;
    }

    /**
     * @brief       Block thread
     * @param tid   Thread ID
     * @return      0 if success, -1 otherwise
     */
    int block_thread(int tid) {
        if (ThreadManager::is_main_thread(tid)) {
            raise_error(ErrorType::threadLibrary, "attempting to block the main thread");
            return ReturnValue::failure;
        }
        if (this->threads[tid]->state == ThreadState::blocked) {
            return ReturnValue::success;
        }
        this->threads[tid]->state = ThreadState::blocked;
        this->ready_threads.erase(
                std::remove(this->ready_threads.begin(), this->ready_threads.end(), tid),
                this->ready_threads.end());
        if (this->running_id == tid) {
            this->manage_ready();
        }
        return ReturnValue::success;
    }


    /**
     * @brief       Resume thread
     * @param tid   Thread ID
     * @return      0 if success, -1 otherwise
     */
    int resume_thread(int tid) {
        if (this->threads[tid]->state == blocked) {
            this->threads[tid]->state = ready;
            this->ready_threads.push_back(tid);
        }
        return ReturnValue::success;

    }

    void time() {
        this->timer.it_value.tv_sec = this->quantum_usecs / (int) 1e6;
        this->timer.it_value.tv_usec = this->quantum_usecs % (int) 1e6;
        this->timer.it_interval.tv_sec = 0;
        this->timer.it_interval.tv_usec = 0;
        if (setitimer(ITIMER_VIRTUAL, &this->timer, nullptr)) {
            raise_error(ErrorType::systemError, "timer error");
            clean_exit();
            //exit(1);
        }
    }

    /*
     * Terminate thread
     */
    int terminate(int tid) {
        if (tid == 0) {
            this->main_exit();
        }
        if (this->mutex.locking_thread_id == tid) {
            if (this->unlock_mutex(tid) == ReturnValue::failure) {
                return ReturnValue::failure;
            }
        }
        if (this->threads[tid]->blocked_by_mutex) {
            this->mutex.block_threads.erase(
                    std::remove(this->mutex.block_threads.begin(), this->mutex.block_threads.end(), tid),
                    this->mutex.block_threads.end());
            this->threads[tid]->blocked_by_mutex = false;
        }
        if (this->running_id == tid) {
            this->threads[this->running_id]->state = ThreadState::terminated;
            this->manage_ready();
        }
        this->ready_threads.erase(
                std::remove(this->ready_threads.begin(), this->ready_threads.end(), tid),
                this->ready_threads.end());
        this->free_thread(tid);
        return 0;
    }


    /*
 * block signals using sigprocmask
 */
    bool block_signals() {
        return sigprocmask(SIG_BLOCK, &this->mask, nullptr) != ReturnValue::failure;
    }

    /*
 * Unblock signals using sigprocmask
 */
    bool unblock_signals() {
        return sigprocmask(SIG_UNBLOCK, &this->mask, nullptr) != ReturnValue::failure;
    }

    /*
     * Clean exit
     */
    int clean_exit() {
        // todo free memory
        for (auto & thread : this->threads) {
            delete thread;
            thread = nullptr;
        }
        free(this);
        exit(-1);
    }

    /*
    * Clean exit
    */
    int main_exit() {
        // todo free memory
        for (auto & thread : this->threads) {
            thread = nullptr;
        }
        free(this);
        exit(0);
    }

};

// need to use singleton
ThreadManager *ThreadManager::manager;

/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * the length of a quantum in micro-seconds. It is an error to call this
 * function with non-positive quantum_usecs.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs) {
    if (quantum_usecs < 0) {
        raise_error(ErrorType::threadLibrary, "invalid input");
        return ReturnValue::failure;
    }
    ThreadManager::manager = new ThreadManager(quantum_usecs); // not sure if memory should be allocated
    if (!ThreadManager::manager) {
        raise_error(ErrorType::systemError, "memory allocation failed");
        return ReturnValue::failure;
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
int uthread_spawn(void (*f)()) {
    if (ThreadManager::manager->get_thread_count() > MAX_THREAD_NUM) {
        raise_error(ErrorType::threadLibrary, "reached maximum number of threads");
        return ReturnValue::failure;
    }
    if (!f) {
        raise_error(ErrorType::threadLibrary, "invalid input");
        return ReturnValue::failure;
    }
    if (!ThreadManager::manager->block_signals()) // block the singls
    {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    int id = ThreadManager::manager->get_new_id();
    int retVal = ThreadManager::manager->init_thread(id, f);
    if (!ThreadManager::manager->unblock_signals()) // unblock the singls
    {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    return retVal;
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
int uthread_terminate(int tid) {
    if (!ThreadManager::manager->id_exist(tid)) {
        raise_error(ErrorType::threadLibrary, "thread doesn't exist");
        return ReturnValue::failure;
    }
    if (!ThreadManager::manager->block_signals()) // block the singls
    {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    int retVal = ThreadManager::manager->terminate(tid);
    if (!ThreadManager::manager->unblock_signals()) // unblock the singls
    {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    return retVal;
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
int uthread_block(int tid) {
    if (!ThreadManager::manager->id_exist(tid)) {
        raise_error(ErrorType::threadLibrary, "thread doesn't exist");
        return ReturnValue::failure;
    }
    if (!ThreadManager::manager->block_signals()) // block the singls
    {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    int retValue = ThreadManager::manager->block_thread(tid);
    if (!ThreadManager::manager->unblock_signals()) // unblock the singls
    {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    return retValue;
}

/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state if it's not synced. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid) {
    if (!ThreadManager::manager->id_exist(tid)) {
        raise_error(ErrorType::threadLibrary, "thread doesn't exist");
        return ReturnValue::failure;
    }
    if (!ThreadManager::manager->block_signals()) // block the singls
    {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    ThreadManager::manager->resume_thread(tid);
    if (!ThreadManager::manager->unblock_signals()) // unblock the singls
    {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    return ReturnValue::success;
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
int uthread_mutex_lock() {
    if (!ThreadManager::manager->block_signals()) {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    int retVal = ThreadManager::manager->lock_mutex(ThreadManager::manager->get_running_id());
    if (!ThreadManager::manager->unblock_signals()) {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    return retVal;
}

/*
 * Description: This function releases a mutex.
 * If there are blocked threads waiting for this mutex,
 * one of them (no matter which one) moves to READY state.
 * If the mutex is already unlocked, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_unlock() {
    if (!ThreadManager::manager->block_signals()) {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    int retVal = ThreadManager::manager->unlock_mutex(ThreadManager::manager->get_running_id());
    if (!ThreadManager::manager->unblock_signals()) {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    return retVal;
}

/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid() {
    if (!ThreadManager::manager->block_signals()) {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    int retVal = ThreadManager::manager->get_running_id();
    if (!ThreadManager::manager->unblock_signals()) // unblock the singls
    {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    return retVal;
}

/*
 * Description: This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums() {
    if (!ThreadManager::manager->block_signals()) {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    int retValue = ThreadManager::manager->get_quantum_usecs();
    if (!ThreadManager::manager->unblock_signals()) {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    return retValue;
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
int uthread_get_quantums(int tid) {
    if (!ThreadManager::manager->valid_id(tid)) {
        raise_error(ErrorType::threadLibrary, "invalid input");
        return ReturnValue::failure;
    }
    if (!ThreadManager::manager->block_signals()) {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    int retVal = ThreadManager::manager->get_thread_quantum(tid);
    if (!ThreadManager::manager->unblock_signals()) {
        raise_error(ErrorType::systemError, "masking failed");
        ThreadManager::manager->clean_exit();
    }
    return retVal;
}