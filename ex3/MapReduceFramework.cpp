//
// Created by weisler on 01/06/2021.
//

#include <atomic>
#include <pthread.h>
#include <cstdio>
#include <queue>
#include <utility>
#include <algorithm>
#include <map>
#include <semaphore.h>
#include "MapReduceFramework.h"
#include "Barrier.cpp"

using namespace std;

void raise_error(const char *errorMsg) {
    fprintf(stderr, "system error: %s\n", errorMsg);
    exit(1);
}

struct MapObject;

/**
 * Job context struct
 */
struct JobContext {
    InputVec input;
    OutputVec &output;
    std::map<K2 *, IntermediateVec> middleware;
    int inputSize;
    bool waitCalled;
    stage_t stage;
    std::vector<pthread_t> threads;
    std::vector<MapObject> mapObjects;
    std::queue<K2*> keys;
    const MapReduceClient *client;
    std::size_t multiLevelThread;
    std::atomic<size_t> mapReduceCounter;
    std::atomic<size_t> totalPairs;
    std::atomic<size_t> shuffleCounter;
    Barrier reduceBarrier;
    Barrier shuffleBarrier;
    pthread_mutex_t inputMutex;
    pthread_mutex_t reduceMutex;
    pthread_mutex_t outputMutex;
    pthread_mutex_t stageMutex;

    JobContext(const InputVec &_input, OutputVec &_output,
               const MapReduceClient *_client, int levelThread) : input(_input),
                                                            output(_output),
                                                            middleware(),
                                                            inputSize(input.size()),
                                                            waitCalled(false),
                                                            stage(stage_t::MAP_STAGE),
                                                            threads(levelThread),
                                                            mapObjects(),
                                                            client(_client),
                                                            multiLevelThread(levelThread),
                                                            mapReduceCounter(0),
                                                            totalPairs(0),
                                                            shuffleCounter(0),
                                                            reduceBarrier(int(multiLevelThread)),
                                                            shuffleBarrier(int(multiLevelThread) - 1),
                                                            inputMutex(PTHREAD_MUTEX_INITIALIZER),
                                                            reduceMutex(PTHREAD_MUTEX_INITIALIZER),
                                                            outputMutex(PTHREAD_MUTEX_INITIALIZER),
                                                            stageMutex(PTHREAD_MUTEX_INITIALIZER) {
        for (std::size_t i = 0; i < multiLevelThread; i++) {
            mapObjects.emplace_back(this);
        }
    }

    virtual ~JobContext() {
        pthread_mutex_destroy(&inputMutex);
        pthread_mutex_destroy(&outputMutex);
        pthread_mutex_destroy(&reduceMutex);
        pthread_mutex_destroy(&stageMutex);
    }
} typedef JobContext;

/**
 * the object given as args when initialize a thread
 * job- pointer to the job
 * mapPairs - the queue of mapPairs objects
 * mapMutex- the mutex used by this thread
 *
 */
struct MapObject {
    JobContext *job;
    std::queue<IntermediatePair> mapPairs;
    pthread_mutex_t mapMutex;

    explicit MapObject(JobContext *job) : job(job), mapMutex(PTHREAD_MUTEX_INITIALIZER) {}
    virtual ~MapObject() {
        pthread_mutex_destroy(&mapMutex);
    }
} typedef MapObject;

/**
 * Lock giving mutex
 * @param mutex pthread_mutex_t pointer
 */
void lockMutex(pthread_mutex_t *mutex) {
    if (pthread_mutex_lock(mutex)) {
        raise_error("Mutex lock failed");
    }
}

/**
 * Unlock giving mutex
 * @param mutex pthread_mutex_t pointer
 */
void unlockMutex(pthread_mutex_t *mutex) {
    if (pthread_mutex_unlock(mutex)) {
        raise_error("Mutex unlock failed");
    }
}

/**
 * This function produces a (K2*, V2*) pair. It has the following signature:
    The function receives as input intermediary element (K2, V2) and context which contains
    data structure of the thread that created the intermediary element. The function saves the
    intermediary element in the context data structures. In addition, the function updates the
    number of intermediary elements using atomic mapReduceCounter.
    Please pay attention that emit2 is called from the client's map function and the context is
    passed from the framework to the client's map function as parameter.
 * @param key K2 object
 * @param value V2
 * @param context
 */
void emit2(K2 *key, V2 *value, void *context) {
    auto map = (MapObject *) context;
    lockMutex(&map->mapMutex);

    map->mapPairs.push(std::make_pair(key, value));
    map->job->totalPairs++;

    unlockMutex(&map->mapMutex);
}

/**
 * This function produces a (K3*, V3*) pair. It has the following signature:
    The function receives as input output element (K3, V3) and context which contains data
    structure of the thread that created the output element. The function saves the output
    element in the context data structures (output vector). In addition, the function updates the
    number of output elements using atomic mapReduceCounter.
    Please pay attention that emit3 is called from the client's map function and the context is
    passed from the framework to the client's map function as parameter.
 * @param key
 * @param value
 * @param context
 */
void emit3(K3 *key, V3 *value, void *context) {
    auto job = (JobContext *) context;

    lockMutex(&job->outputMutex);
    job->output.push_back(std::make_pair(key, value));
    unlockMutex(&job->outputMutex);
}

/*
 * Map single thread
 */
void *mapThread(MapObject * mapObject) {
    if (mapObject == nullptr) {
        return nullptr;
    }
    while (true) {
        lockMutex(&mapObject->job->inputMutex);
        if (mapObject->job->input.empty())
        {
            unlockMutex(&mapObject->job->inputMutex);
            break;
        }

        auto inPair = mapObject->job->input.back();
        mapObject->job->input.pop_back();
        unlockMutex(&mapObject->job->inputMutex);
        mapObject->job->client->map(inPair.first, inPair.second, mapObject); // call the clients map
        mapObject->job->mapReduceCounter++;
    }
    //std::sort(mapObject->mapPairs.begin(), mapObject->mapPairs.end());//todo: Not Kosher
    return nullptr;
}

/*
 * Reduce single thread
 */
void *reduceThread(JobContext * job) {
    job->reduceBarrier.barrier();
    lockMutex(&job->stageMutex);

    if (job->stage != REDUCE_STAGE) {
        job->stage = REDUCE_STAGE; //Set stage tp shuffle
        job->mapReduceCounter = 0;
        job->inputSize = job->keys.size();
    }
    unlockMutex(&job->stageMutex);
    while(true) {
        lockMutex(&job->reduceMutex);
        if (job->keys.empty()){
            unlockMutex(&job->reduceMutex);
            break;
        }
        auto key = job->keys.front();
        job->keys.pop();
        job->mapReduceCounter++;
        unlockMutex(&job->reduceMutex);
        job->client->reduce(&job->middleware.at(key), job);
    }
    return nullptr;
}

/**
 * mapObjects the input to threads
 * @param m
 */
void *startMap(void *mapArgs) {
    auto map = (MapObject *) mapArgs;
    mapThread(map);

    map->job->shuffleBarrier.barrier();

    lockMutex(&map->job->stageMutex);

    map->job->stage = SHUFFLE_STAGE; //Set stage tp shuffle

    unlockMutex(&map->job->stageMutex);

    reduceThread(map->job);
    return nullptr;
}

void shuffleThreads(JobContext * job) {
    for(std::size_t i = 0; i < job->multiLevelThread; i++) {
        auto map = &job->mapObjects[i];
        lockMutex(&map->mapMutex);
        while (!map->mapPairs.empty()) {
            auto pair = map->mapPairs.front();
            map->mapPairs.pop();
            unlockMutex(&map->mapMutex);
            if(job->middleware.find(pair.first) == job->middleware.end()) {
                job->keys.push(pair.first);
                job->middleware[pair.first].emplace_back(pair);
            } else {
                job->middleware[pair.first].push_back(pair);
            }
            job->shuffleCounter++;
        }
        unlockMutex(&map->mapMutex);
    }
}

/**
 *
 * @param inputJob
 * @return
 */
void *startMapWithShuffle(void *inputJob) {
    auto map = (MapObject *) inputJob;
    //mapThread(&map->job->mapObjects.at(map->job->multiLevelThread - 1));
    while (map->job->stage == MAP_STAGE)
    {
        shuffleThreads(map->job);
    }
    shuffleThreads(map->job);
    reduceThread(map->job);
    return nullptr;
}


JobHandle startMapReduceJob(MapReduceClient *client, const InputVec& inputVec, OutputVec &outputVec,
                            int multiThreadLevel) {
    auto job = new JobContext(inputVec, outputVec, client, multiThreadLevel);
    for (size_t i = 0; i < job->multiLevelThread - 1; i++) {
        if (pthread_create(&job->threads[i], nullptr, &startMap, &job->mapObjects[i])) {
            raise_error("Thread creation failed");
        }
    }
    if (pthread_create(&job->threads[multiThreadLevel - 1], nullptr, &startMapWithShuffle,
                       &job->mapObjects[multiThreadLevel - 1])) {
        raise_error("Thread creation failed");
    }
    return job;
}

/**
 * a function gets JobHandle returned by startMapReduceFramework and waits
    until it is finished.
    Hint – you should use the c function pthread_join.
    It is legal to call the function more than once and you should handle it. Pay attention that
    calling pthread_join twice from the same process has undefined behavior and you must
    avoid that.
 * @param job JobHandle
 */
void waitForJob(JobHandle job) {
    auto context = (JobContext *) job;
    if (!context->waitCalled) {
        fprintf(stdout, "wait1\n");
        context->waitCalled = true;
        for (std::size_t i = 0; i < context->multiLevelThread; i++) {
            if (pthread_join(context->threads[i], nullptr)) {
                raise_error("pthread_join failed");
            }
            fprintf(stdout, "wait\n");
        }
    }
    context->stage = UNDEFINED_STAGE;
}

/**
 * this function gets a JobHandle and updates the state of the job into the given JobState struct.
 * @param job       JobHandle
 * @param state     JobState
 */
void getJobState(JobHandle job, JobState *state) {
    auto context = (JobContext *) job;

    lockMutex(&context->stageMutex);

    state->stage = context->stage;
    if (context->stage == UNDEFINED_STAGE) {
        state->percentage = 0;
    } else if (context->stage == SHUFFLE_STAGE) {
        state->percentage = ((float) context->shuffleCounter / context->totalPairs) * 100;
    } else {
        state->percentage = ((float) context->mapReduceCounter / context->inputSize) * 100;
    }

    unlockMutex(&context->stageMutex);
}

/**
 * Releasing all resources of a job. You should prevent releasing resources
    before the job finished. After this function is called the job handle will be invalid.
    In case that the function is called and the job is not finished yet wait until the job is
    finished to close it.
    In order to release mutexes and semaphores (pthread_mutex, sem_t) you should use the
    functions pthread_mutex_destroy, sem_destroy.
 * @param job    JobHandle
 */
void closeJobHandle(JobHandle job) {
    auto context = (JobContext *) job;
    waitForJob(job);
    delete context;
}

