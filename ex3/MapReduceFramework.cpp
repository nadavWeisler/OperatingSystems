//
// Created by weisler on 01/06/2021.
//

#include <zconf.h>
#include "MapReduceFramework.h"

using namespace std;

/**
 * Job contex struct
 */
struct JobContex {
    InputVec input;
    OutputVec output;
    IntermediateVec middleware;
    int percentage;
    stage_t stage;
    std::vector<pthread_t> threads;
    MapReduceClient *client;
    int multiLevelThread;

    JobContex(InputVec _input, OutputVec _output, MapReduceClient *_client, int levelThread) : input(_input),
                                                                                               output(_output),
                                                                                               client(_client),
                                                                                               multiLevelThread(
                                                                                                       levelThread),
                                                                                               threads(levelThread),
                                                                                               stage(stage_t::UNDEFINED_STAGE),
                                                                                               percentage(0) {
    }
} typedef JobContex;

/**
 * This function produces a (K2*, V2*) pair. It has the following signature:
    The function receives as input intermediary element (K2, V2) and context which contains
    data structure of the thread that created the intermediary element. The function saves the
    intermediary element in the context data structures. In addition, the function updates the
    number of intermediary elements using atomic counter.
    Please pay attention that emit2 is called from the client's map function and the context is
    passed from the framework to the client's map function as parameter.
 * @param key
 * @param value
 * @param context
 */
void emit2(K2 *key, V2 *value, void *context) {
    auto job = (JobContex *) context;
    job->middleware.push_back(std::make_pair(key, value));
}

/**
 * This function produces a (K3*, V3*) pair. It has the following signature:
    The function receives as input output element (K3, V3) and context which contains data
    structure of the thread that created the output element. The function saves the output
    element in the context data structures (output vector). In addition, the function updates the
    number of output elements using atomic counter.
    Please pay attention that emit3 is called from the client's map function and the context is
    passed from the framework to the client's map function as parameter.
 * @param key
 * @param value
 * @param context
 */
void emit3(K3 *key, V3 *value, void *context) {
    auto job = (JobContex *) context;
    job->output.push_back(std::make_pair(key, value));
}

JobHandle startMapReduceJob(const MapReduceClient &client,
                            const InputVec &inputVec, OutputVec &outputVec,
                            int multiThreadLevel) {
    auto job = JobContex
    // We get V2 from the client Map
    // Sort(K2); (shuffle)
    //K3, V3 = Client.reduce(K2, V2)
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

}

/**
 * this function gets a JobHandle and updates the state of the job into the given JobState struct.
 * @param job       JobHandle
 * @param state     JobState
 */
void getJobState(JobHandle job, JobState *state) {
    //job.state = state;
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
    //free(all)
}