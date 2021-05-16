#include "uthreads.h"
#include <queue>
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/time.h>
#include <algorithm>

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

typedef State enum ThreadState
{
	ready,
	blocked,
	running
};

typedef MutexState enum MutexState
{
	locked,
	free
};

struct Thread
{
	int ID;
	int quantum;
	State state;
	sigjmp_buf env; // an array keeping al the data of the thread
	char stack[STACK_SIZE];
} typedef thread;

struct Mutex
{
	int state;
	int running_thread_id;
	int *blocked_list;
	int num_of_blocked;
} typedef Mutex;

class ThreadManager
{
private:
	int running_id = 0;
	int thread_count = 0;
	int quantum_usecs;
	int total_quantum = 0;
	queue<int> ready_threads;
	Mutex mutex = {MutexState::free, 0, nullptr};
	Thread *threads[MAX_THREAD_NUM]; //
    sigset_t mask;
    struct itimerval timer{}; // timer

public
    //Singleton
    static * ThreadManager manager;

    /**
     * @brief                   Singleton constructor
     * @param quantum_usecs     Thread max running time.
     */
    ThreadManager(int quantum_usecs): threads(), ready_threads(), mask(){
        // todo need to add sigaddset, sigempty set,  SIGVTALRM,sa_hanlder
        this->quantum_usecs = quantum_usecs;

        for (int i  =0; i < MAX_THREAD_NUM; i++)
        {
            threads[i] = nullptr;
        }
        // init first thread
        threads[0] = {0, 1,running, nullptr};
        this->thread_count++;
    }

    /**
     * @brief       Init thread
     * @param id    Thread ID
     * @param f     Address
     * @return      0 if success, -1 otherwise
     */
	int init_thread(int id, void *f) {
        Thread newThread = {id, 0, ready, f};
        address_t sp, pc;
        sp = (address_t)stack1 + STACK_SIZE - sizeof(address_t);
        pc = (address_t)f;
        sigsetjmp(env[0], 1);
        (newThread->env->__jmpbuf)[JB_SP] = translate_address(sp);
        (newThread->env->__jmpbuf)[JB_PC] = translate_address(pc);
        if (sigemptyset(&env[0]->__saved_mask))
        {
            // error message
            exit(-1)
        }
        // need to use sigsetjmp to save the env of the thread
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
        if (sigsetjmp(this->threads[this->running_id]->env, 1))
        {
            fprintf(stderr, "system error: masking failed");
            return -1;
        }
        if (this->threads[this->running_id]->state == running) // if running is blocked
        {
            this->ready_threads.push(this->running_id);
            this->threads[this->running_id]->state = ready;
        }
        this->running_id = this->ready_threads.pop_front();
        this->threads[this->running_id]->quantum++;
        this->total_quantum++;
        this->threads[this->running_id]->state = running;
        siglongjmp(this->threads[this->running_id]->env, 1)
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
	int set_to_block(int tid)
	{
        if (tid == 0)
        {
            fprintf(stderr, "thread library error: attempting to block the main thread\n");
            return -1;
        }
        if (this->threads[tid]->state == blocked) // allready blocked
        {
            return 0;
        }
		this->threads[tid]->state = blocked;
        // need to remove from ready q
        if (this->running_id == tid) // blocked thred is currenlty running
        {
            this->manage_ready();
        }
	}

	/**
	 * @brief       Resume thread
	 * @param tid   Thread ID
	 * @return      0 if success, -1 otherwise
	 */
	int resume_thread(int tid)
    {
        if (this->threads[tid] != blocked) // thread dosnt need to be freed
        {
            return 0;
        }
        this->threads[tid]->state = ready;
        this->ready_threads.push_back(threads[tid])
    }

    int time()
    {
	    timer.it_value.tv_sec = this->quantum_usecs / (int) 1e6;
	    timer.it_value.tv_usec = this->quantum_usecs % (int) 1e6;
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_usec = 0;
        if (setitimer(ITIMER_VIRTUAL, &timer, nullptr)) {
            fprintf(stderr, "system error: timer error\n");
            clean_exit();
            exit(1);
        }
    }

    int clean_exit() {
        // todo free memeory
        for (int i = 0; i < MAX_THREAD_NUM; i++)
        {
            this->threads[i] = nullptr;
        }
        free(this);
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
        fprintf(stderr, "thread library error: invalid input\n");
		return -1;
	}
	ThreadManager::manager = new ThreadManager(quantum_usecs); // not sure if memory should be allocted
	// need to add sigaddset, sigempty set,  SIGVTALRM,sa_hanlder
	if (!ThreadManager::manager)
    {
        printf(stderr, "system error: memory allocation failed\n");
        exit(1);
    }
	return 0;
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
        fprintf(stderr, "thread library error: reached maximum number of threads\n");
		return -1;
	}
	if (!f)
	{
		fprintf(stderr, "thread library error: invalid input\n");
		return -1;
	}
	int id = manager.get_new_id();
	if (id == -1)
	{
        "thread library error: reached maximum number of threads\n"); // checked by max number?
		return -1;
	}
	ThreadManager::manager.init_thread(id, f);
	ThreadManager::manager.manage_ready();
	return 0;
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
	if (ThreadManager:: manager.valid_id(tid))
	{
		ThreadManager:: manager.free_thread(tid);
		return 0;
	}
    fprintf(stderr, "thread library error: invalid input\n");
	return -1;
	//todo: release thread ID
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
	if (manager.get_new_id() == -1)
	{
        fprintf(stderr, "thread library error: invalid input\n");
		return -1;
	}
	ThreadManager:: manager.set_to_block(tid);
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
	if (manager.get_new_id() == -1)
	{
        fprintf(stderr, "thread library error: invalid input\n");
		return -1;
	}
	ThreadManager::manager.resume_thread();
}

/**
 * @brief   Lock mutex
 * @return  0 if success -1 otherwise
 */
int uthread_mutex_lock()
{
	if (ThreadManager::manager.get_mutex().state == MutexState::free)
	{
		ThreadManager::manager.get_mutex().state == MutexState::free;
		ThreadManager::manager.get_mutex().running_thread_id = manager.get_running_id();
		return 0;
	}
	else if (ThreadManager::manager.get_mutex().state == MutexState::locked)
	{
		if (ThreadManager::manager.get_mutex().running_thread_id == ThreadManager:: manager.get_running_id())
		{
			fprintf(stderr, "thread library error: mutex error\n);
			return -1;
		}
		int block_number = manager.get_mutex().num_of_blocked;
		ThreadManager:: manager.get_mutex().blocked_list[block_number] = ThreadManager:: manager.get_running_id();
		ThreadManager:: manager.set_to_block(ThreadManager::manager.get_running_id());
	}
}

/**
 * @brief   Unlock mutex
 * @return  0 if success -1 otherwise
 */
int uthread_mutex_unlock()
{
	if (ThreadManager::manager.get_mutex().state == MutexState::free)
	{
		fprintf(stderr, "thread library error: mutex error\n);
		return -1;
	}
	return 0;
	//todo: add unlock threads
}

/**
 * @brief   Get running thread ID
 * @return  Running thread ID
 */
int uthread_get_tid()
{
	return ThreadManager::manager.get_running_id();
}

/**
 * @brief   Get total threads quantums
 * @return  Number
 */
int uthread_get_total_quantums()
{
	return ThreadManager:: manager.get_quantum_usecs();
}

/**
 * @brief       Queantum of running thread
 * @param tid   Thread id
 * @return      Number
 */
int uthread_get_quantums(int tid)
{
	if (!ThreadManager::manager.valid_id(tid))
	{
		fprintf(stderr, "thread library error: invalid input\n);
		return -1;
	}
	return ThreadManager::manager.get_thread_quantum(tid);
}