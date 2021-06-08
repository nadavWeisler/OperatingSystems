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
#include "MapReduceFramework.h"
#include "Barrier.cpp"

using namespace std;

void raise_error(const char *errorMsg) {
    fprintf(stderr, "%s\n", errorMsg);
    exit(1);
}

struct MapObject;

/**
 * Job context struct
 */
struct JobContext {
    InputVec input;
    OutputVec output;
    std::map<K2 *, IntermediateVec> middleware;
    int inputSize;
    bool waitCalled;
    stage_t stage;
    std::vector<pthread_t> threads;
    std::vector<MapObject> mapObjects;
    MapReduceClient *client;
    int multiLevelThread;
    std::atomic<size_t> mapReduceCounter;
    std::atomic<size_t> totalPairs;
    std::atomic<size_t> shuffleCounter;
    Barrier reduceBarrier;
    Barrier shuffleBarrier;
    pthread_mutex_t inputMutex;
    pthread_mutex_t reduceMutex;
    pthread_mutex_t outputMutex;
    pthread_mutex_t stageMutex;

    JobContext(const InputVec &_input, const OutputVec &_output,
               MapReduceClient *_client, int levelThread) : input(_input),
                                                            output(_output),
                                                            client(_client),
                                                            multiLevelThread(levelThread),
                                                            threads(levelThread),
                                                            waitCalled(false),
                                                            inputSize(input.size()),
                                                            mapReduceCounter(0),
                                                            totalPairs(0),
                                                            shuffleCounter(0),
                                                            stage(stage_t::MAP_STAGE),
                                                            shuffleBarrier(levelThread - 1),
                                                            reduceBarrier(levelThread),
                                                            inputMutex(PTHREAD_MUTEX_INITIALIZER),
                                                            reduceMutex(PTHREAD_MUTEX_INITIALIZER),
                                                            outputMutex(PTHREAD_MUTEX_INITIALIZER),
                                                            stageMutex(PTHREAD_MUTEX_INITIALIZER) {
        for (int i =0; i< levelThread; i++)
        {
            mapObjects.emplace_back(this);
        }
    }

    virtual ~JobContext()
    {
        pthread_mutex_destroy(&inputMutex);
        pthread_mutex_destroy(&outputMutex);
        pthread_mutex_destroy(&reduceMutex);
        pthread_mutex_destroy(&stageMutex);
    }
} typedef JobContext;

/**
 * the object given as args when initialize a thread
 * job- pointer to the job
 * mapped - the queue of mapped objects
 * mapMutex- the mutex used by this thread
 *
 */
struct MapObject {
    JobContext *job;
    std::vector<IntermediatePair> mapped;
    pthread_mutex_t mapMutex;

    explicit MapObject(JobContext *job) : job(job), mapMutex(PTHREAD_MUTEX_INITIALIZER) {}
    virtual  ~MapObject()
    {
        pthread_mutex_destroy(&mapMutex);
    }
} typedef MapObject;


/**
 * This function produces a (K2*, V2*) pair. It has the following signature:
    The function receives as input intermediary element (K2, V2) and context which contains
    data structure of the thread that created the intermediary element. The function saves the
    intermediary element in the context data structures. In addition, the function updates the
    number of intermediary elements using atomic mapReduceCounter.
    Please pay attention that emit2 is called from the client's map function and the context is
    passed from the framework to the client's map function as parameter.
 * @param key
 * @param value
 * @param context
 */
void emit2(K2 *key, V2 *value, void *context) {
    auto map = (MapObject *) context;
    if (pthread_mutex_lock(&map->mapMutex)) { // locking mutex failed
        raise_error("Mutex lock failed");
    }
    map->mapped.emplace_back(key, value);
    map->job->totalPairs++;
    if (pthread_mutex_unlock(&map->mapMutex)) {
        raise_error("Mutex unlock failed");
    }
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

    if (pthread_mutex_lock(&job->outputMutex)) { // locking mutex failed
        raise_error("Mutex lock failed");
    }
    job->output.push_back(std::make_pair(key, value));
    if (pthread_mutex_unlock(&job->outputMutex)) {
        raise_error("Mutex unlock failed");
    }
}

void *mapThread(MapObject * mapObject) {
    while (!mapObject->job->input.empty()) {
        if (pthread_mutex_lock(&mapObject->job->inputMutex)) { // locking mutex failed
            raise_error("Mutex lock failed");
        }
        auto inPair = mapObject->job->input.back();
        mapObject->job->input.pop_back();
        if (pthread_mutex_unlock(&mapObject->job->inputMutex)) {
            raise_error("Mutex unlock failed");
        }
        mapObject->job->client->map(inPair.first, inPair.second, mapObject); // call the clients map
        mapObject->job->mapReduceCounter++;
    }
    std::sort(mapObject->mapped.begin()->second, mapObject->mapped.end()->second);//todo: Not Kosher
}

void *reduceThread(JobContext * job) {
    job->reduceBarrier.barrier();
    if (job->stage != REDUCE_STAGE) {
        if (pthread_mutex_lock(&job->stageMutex)) { // switching stage mutex lock
            raise_error("Mutex lock stage switch failed");
        }

        job->stage = REDUCE_STAGE; //Set stage tp shuffle
        job->mapReduceCounter = 0;

        if (pthread_mutex_unlock(&job->stageMutex)) {
            raise_error("Mutex unlock failed");
        }
    }
    auto it = job->middleware.begin();
    // Iterate over the map using Iterator till end.
    while (it != job->middleware.end()) {
        if (pthread_mutex_lock(&job->reduceMutex)) { // switching stage mutex lock
            raise_error("Reduce mutex lock failed");
        }
        job->mapReduceCounter++;
        job->client->reduce(&job->middleware[it->first], job);
        if (pthread_mutex_unlock(&job->reduceMutex)) {
            raise_error("Reduce mutex unlock failed");
        }
        it++;
    }
}

/**
 * mapObjects the input to threads
 * @param m
 */
void *startMap(void *mapArgs) {
    auto m = (MapObject *) mapArgs;
    mapThread(m);

    m->job->shuffleBarrier.barrier();

    if (pthread_mutex_lock(&m->job->stageMutex)) { // switching stage mutex lock
        raise_error("Mutex lock stage switch failed");
    }

    m->job->stage = SHUFFLE_STAGE; //Set stage tp shuffle

    if (pthread_mutex_unlock(&m->job->stageMutex)) {
        raise_error("Mutex unlock failed");
    }
    reduceThread(m->job);
}

//Todo:: MUTEXXXXXX
void *shuffleThreads(JobContext * job) {
    bool finish = false;
    while (finish < job->multiLevelThread) {
        for (auto &jobMap : job->mapObjects) {
            if (pthread_mutex_lock(&jobMap.mapMutex)) { // switching stage mutex lock
                raise_error("Mutex lock map object failed");
            }
            if (!jobMap.mapped.empty()) {
                auto pair = jobMap.mapped.back();
                jobMap.mapped.pop_back();
                job->middleware[pair.first].emplace_back(pair);
                job->shuffleCounter++;
            } else { //TODO: Not kosher
                finish = true;
                for (int i = 0; i < job->multiLevelThread; i++) {
                    if (!job->mapObjects.empty()) {
                        finish = false;
                    }
                }
            }
            if (pthread_mutex_unlock(&jobMap.mapMutex)) {
                raise_error("Mutex unlock failed");
            }
        }

    }
}

/**
 *
 * @param inputJob
 * @return
 */
void *startMapWithShuffle(void *inputJob) {
    auto job = (JobContext *) inputJob;
    mapThread(&job->mapObjects[0]);
    if (job->stage == SHUFFLE_STAGE) {
        shuffleThreads(job);
    }
    reduceThread(job);
}


JobHandle startMapReduceJob(MapReduceClient *client, const InputVec &inputVec, OutputVec &outputVec,
                            int multiThreadLevel) {
    auto job = new JobContext(inputVec, outputVec, (MapReduceClient *) client, multiThreadLevel);
    for (int i = 0; i < multiThreadLevel; i++) {
        if (i == 0) {
            if (pthread_create(&job->threads[0], nullptr, &startMapWithShuffle, &job)) {
                raise_error("Thread creation failed");
            }
        } else if (pthread_create(&job->threads[i], nullptr, &startMap, &job->mapObjects[i])) {
            raise_error("Thread creation failed");
        }
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
        context->waitCalled = true;
        for (int i = 0; i < context->multiLevelThread; i++) {
            if (pthread_join(context->threads[i], nullptr)) {
                raise_error("pthread_join failed");
            }
        }
    }
}

/**
 * this function gets a JobHandle and updates the state of the job into the given JobState struct.
 * @param job       JobHandle
 * @param state     JobState
 */
void getJobState(JobHandle job, JobState *state) {
    auto context = (JobContext *) job;
    if (pthread_mutex_lock(&context->stageMutex)) { // switching stage mutex lock
        raise_error("Mutex lock stage switch failed");
    }

    state->stage = context->stage;
    if (context->stage == UNDEFINED_STAGE)
    {
        state->percentage = 0;
    }
    else if (context->stage == SHUFFLE_STAGE) {
        state->percentage = ((float) context->shuffleCounter / context->totalPairs) * 100;
    } else {
        state->percentage = ((float) context->mapReduceCounter / context->inputSize) * 100;
    }

    if (pthread_mutex_unlock(&context->stageMutex)) {
        raise_error("Mutex unlock failed");
    }
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