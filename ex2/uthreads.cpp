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
	sigjmp_buf env;
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
	int quantum_usecs = 0;
	int stack = 0; // add a stack
	Thread *threads[MAX_THREAD_NUM];
	queue<Thread> ready_threads;
	Mutex m = {MutexState::free, 0, nullptr};

public:
	int init_thread(int id, void *f)
	{
		Thread newThread = {id, this->quantum_usecs, ready, f};
		this->thread_count++;
		ready_threads.push(newThread);
		this->threads[id] = &newThread;
		return id;
	}

	void init_first(int quantum_usec)
	{
		this->quantum_usecs = quantum_usec;
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
	}

	bool valid_id(int id)
	{
		return this->threads[id] != nullptr && id <= MAX_THREAD_NUM && id >= 0;
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
		this->threads[tid]->state = blocked;
	}
};

ThreadManager manager;

int uthread_init(int quantum_usecs)
{
	if (quantum_usecs < 0)
	{
		return -1;
	}
	manager.init_first(quantum_usecs);
	manager.init_thread(0, nullptr); // inits the main tread
	return 0;
}

int uthread_spawn(void (*f)(void))
{
	if (manager.get_thread_count() == MAX_THREAD_NUM)
	{
		return -1;
	}
	if (!f)
	{
		fprintf(stderr, "ERROR: f is missing");
		return -1;
	}
	int id = manager.get_new_id();
	if (id == -1)
	{
		return -1;
		fprintf(stderr, "ERROR: invalid id");
	}
	manager.init_thread(id, f);
	manager.manage_ready();
	return 0;
}

int uthread_terminate(int tid)
{
	if (manager.valid_id(tid))
	{
		manager.free_thread(tid);
		return 0;
	}
	return -1;
}

int uthread_block(int tid)
{
	if (manager.get_new_id() == -1)
	{
		fprintf(stderr, "uthread_block");
		return -1;
	}
	manager.set_to_block(tid);
}

int uthread_resume(int tid)
{
	if (manager.get_new_id() == -1)
	{
		fprintf(stderr, "uthread_block");
		return -1;
	}
	//todo: Resume thread
}

int uthread_mutex_lock()
{
	if (manager.get_mutex().state == MutexState::free)
	{
		manager.get_mutex().state == MutexState::free;
		manager.get_mutex().running_thread_id = manager.get_running_id();
		return 0;
	}
	else if (manager.get_mutex().state == MutexState::locked)
	{
		if (manager.get_mutex().running_thread_id == manager.get_running_id())
		{
			fprintf(stderr, "uthread_mutex_lock");
			return -1;
		}
		int block_number = manager.get_mutex().num_of_blocked;
		manager.get_mutex().blocked_list[block_number] = manager.get_running_id();
		manager.set_to_block(manager.get_running_id());
	}
}

int uthread_mutex_unlock()
{
	if (manager.get_mutex().state == MutexState::free)
	{
		fprintf(stderr, "ERROR: thread is already free");
		return -1;
	}
	//todo: add unlock threads
}

int uthread_get_tid()
{
	return manager.get_running_id();
}

int uthread_get_total_quantums()
{
	return manager.get_quantum_usecs();
}

int uthread_get_quantums(int tid)
{
	if (!manager.valid_id(tid))
	{
		fprintf(stderr, "ERROR: invalid thread ID");
		return -1;
	}
	return manager.get_thread_quantum(tid);
}