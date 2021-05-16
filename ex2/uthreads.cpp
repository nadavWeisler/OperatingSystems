using namespace std;

#include <queue>
#include "uthreads.h"

typedef enum State
{
	ready,
	blocked,
	running
};

typedef enum MutexState
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
	int ID = 0;
	int thread_count = 0;
	int quantum_usecs;
	int total_quantum = 0;
	int stack = 0; // add a stack // why?
	Thread *threads[MAX_THREAD_NUM]; //
	queue<Thread> ready_threads; // need to changes to int or pointer
	Mutex m = {MutexState::free, 0, nullptr};
    struct itimerval timer{}; // timer

public
    //singelton
    static * ThreadManager manager;
    ThreadManager(int quantum_usecs): threads(), ready_threads(),
    {
        this->quantum_usecs = quantum_usecs;
        for (int i  =0; i < MAX_THREAD_NUM; i++)
        {
            threads[i] = nullptr;
        }
        // init first thread
        threads[0] = {0, quantum_usecs,running, nullptr };
        this->thread_count++;
    }

    // use for spawn

	int init_thread(int id, void *f) {
        Thread newThread = {id, this->quantum_usecs, ready, f};
        // need to use sigsetjmp to save the env of the thread
        this->thread_count++;
        ready_threads.push(newThread);
        this->threads[id] = &newThread;
        return id;
    }

	int get_thread_count()
	{
		return this->thread_count;
	}

	int get_new_id()
	{
		//todo: I think we should find more efficient way
		for (int i = 0; i < MAX_THREAD_NUM; i++)
		{
			if (this->threads[i] == nullptr)
			{
				return i;
			}
		}
		return -1;
	}

	int manage_ready()
	{
		//todo: moves the threads from ready to running
		// need to use siglongjmp
	}

	bool valid_id(int id)
	{
		return this->threads[id] != nullptr && id <= MAX_THREAD_NUM && id >= 0;
	}

	int free_thread(int id)
	{
        free(this->threads[id])
		this->threads[id] = nullptr;
        this->thread_count--;
	}

	int get_quantum_usecs()
	{
		return this->total_quantum;
	}

	int get_thread_quantum(int tid)
	{
		return this->threads[tid]->quantum;
	}

	Mutex &get_mutex()
	{
		return this->m;
	}

	int get_running_id()
	{
		return this->ID;
	}

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
        if (this->ID == tid) // blocked thred is currenlty running
        {
            this->manage_ready();
        }
	}

	int resume_thread(int tid)
    {
        if (this->threads[tid] != blocked) // thread dosnt need to be freed
        {
            return 0;
        }
        this->threads[tid]->state = ready;
        this->ready_threads.push_back(threads[tid])

    }
};

// need to use singeltom
ThreadManager manager;

int uthread_init(int quantum_usecs)
{
	if (quantum_usecs < 0)
	{
        fprintf(stderr, "thread library error: invalid input\n");
		return -1;
	}
	ThreadManager::manager = new ThreadManager(quantum_usecs); // not sure if memory should be allocted
	if (!ThreadManager::manager)
    {
        printf(stderr, "system error: memory allocation failed\n");
        exit(1);
    }
	return 0;
}

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

int uthread_terminate(int tid)
{
	if (ThreadManager:: manager.valid_id(tid))
	{
		ThreadManager:: manager.free_thread(tid);
		return 0;
	}
    fprintf(stderr, "thread library error: invalid input\n");
	return -1;
}

int uthread_block(int tid)
{
	if (manager.get_new_id() == -1)
	{
        fprintf(stderr, "thread library error: invalid input\n");
		return -1;
	}
	ThreadManager:: manager.set_to_block(tid);
}

int uthread_resume(int tid)
{
	if (manager.get_new_id() == -1)
	{
        fprintf(stderr, "thread library error: invalid input\n");
		return -1;
	}
	ThreadManager::manager.resume_thread();
}

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

int uthread_mutex_unlock()
{
	if (ThreadManager::manager.get_mutex().state == MutexState::free)
	{
		fprintf(stderr, "thread library error: mutex error\n);
		return -1;
	}
	//todo: add unlock threads
}

int uthread_get_tid()
{
	return ThreadManager:: manager.get_running_id();
}

int uthread_get_total_quantums()
{
	return ThreadManager:: manager.get_quantum_usecs();
}

int uthread_get_quantums(int tid)
{
	if (!ThreadManager::manager.valid_id(tid))
	{
		fprintf(stderr, "thread library error: invalid input\n);
		return -1;
	}
	return ThreadManager::manager.get_thread_quantum(tid);
}