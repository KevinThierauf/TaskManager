#include <TaskScheduler.hpp>
#include <Worker.hpp>
#include <iostream>

// some extraneous helper classes/functions for cout. You can ignore this

// cout *is* thread-safe, but multiple print calls may be printed in
// indeterminate order
// this is a simple wrapper which guarantees that all print calls in a single call chain will be printed in order
struct SyncOut {
    std::ostream &stream;

    explicit SyncOut(std::ostream &stream) : stream(stream) {}
};

struct SyncState {
    static std::mutex mutex;
    std::ostream &stream;
    std::scoped_lock<std::mutex> lock;

    SyncState(SyncOut &out) : stream(out.stream), lock(std::scoped_lock(mutex)) { // NOLINT(google-explicit-constructor)
    }
};

std::mutex SyncState::mutex;

template<typename T>
const SyncState &operator<<(const SyncState &state, const T &value) {
    state.stream << value;
    return state;
}

// ostream manipulator
const SyncState &operator<<(const SyncState &state, std::ostream &(*f)(std::ostream &)) {
    f(state.stream);
    return state;
}

// end of helper stuff

int main() {
    using namespace tskm;
    using namespace std::chrono;

    SyncOut out(std::cout);
    SyncOut err(std::cerr);

    // tasks can be assigned to a schedule
    // the most basic TaskSchedule is a TaskQueue, which simply queues up
    // tasks in the order they are received
    // the TaskSchedule itself does not determine how tasks are executed (e.g.
    // sync, async); this behavior is left up to Worker(s). The TaskSchedule simply
    // assigns tasks to a worker
    TaskQueue queue; // create TaskQueue

    // create an async worker shared_ptr
    // the worker will pull tasks from the given TaskSchedule (in this case, a TaskQueue)
    // the worker will start immediately, unless a DelayedStart is explicitly provided (as shown below)
    auto worker = worker::async(queue);

    // create an empty DelayedStart object
    DelayedStart delayStart;
    assert(delayStart.isEmpty());

    // pass the delayedStart value during worker creation
    auto worker2 = worker::async(queue, &delayStart);
    assert(!delayStart.isEmpty());
    // worker2 will not start until the delayStart.start() method is called, the delayStart
    // variable is destroyed (e.g. goes out of scope), or the delayStart variable is given to
    // another worker (in which case it will call start before accepting next worker)
    delayStart.start();
    // delayStart is once again empty
    assert(delayStart.isEmpty());

    // add task1 to queue
    queue.addTask([&]() {
        out << "Task 1: Sleeping for 500ms" << std::endl;
        std::this_thread::sleep_for(500ms);
        out << "Task 1: Sleep over" << std::endl;
    });

    const char *str = "Hello, World!";
    // tasks can be given parameters
    queue.addTask([&](unsigned num, const std::string &str) {
        out << "Task 2: Sleeping for " << num << "ms" << std::endl;
        std::this_thread::sleep_for(milliseconds(num));
        out << "Task 2: " << str << std::endl;
    }, 5, str);

    // tasks can also return values
    // the returned value can be accessed using a TypedFutureTaskResult
    TypedFutureTaskResult<double> thirdTaskFuture = queue.addTask([]() {
        return 2.0;
    });

    // the future will block until the value becomes available
    // the return value will be nullptr if the task5 does not return successfully
    // in any other case, the return value will not be nullptr
    if (const double *value = thirdTaskFuture.getReturnValue()) {
        out << "Task 3 returned " << *value << std::endl;
    } else assert(false);

    // a future to a void function can also be obtained
    TypedFutureTaskResult<void> fourthTaskFuture = queue.addTask([]() {
        throw std::runtime_error("task4 throwing exception example");
    });

    // instead of checking the return value (since there isn't one!) the success() function
    // can be used to verify the task5 finished normally, instead
    if (fourthTaskFuture.success()) {
        assert(false);
    } else {
        // get exception
        if (std::optional<std::string> errorMessage = fourthTaskFuture.getResult().getExceptionMessage()) {
            err << "Task 4 failed with the following error message: " << *errorMessage << std::endl;
        } // else cancelled
    }

    // create a task5, but don't assign it to any schedule
    auto pair = Task::create([]() {});
    Task task5 = std::move(pair.first);
    TypedFutureTaskResult<void> fifthTaskFuture = std::move(pair.second);

    // tasks can be executed manually, exactly as-is (synchronously), using the () operator
    task5();

    // a FutureTaskResult of any template parameters can be converted to a FutureTaskResult, a generic type which 
    // can be used to access all of the same data, but without the type information
    FutureTaskResult genericFifthTaskFuture = fifthTaskFuture;

    // the return value can be obtained through a void pointer
    // if the Task is a void function, the return value will be a dummy value to
    // stay consistent with the rest of the api. The dummy value itself is unspecified, and of an unspecified type
    if (const void *value = genericFifthTaskFuture.getReturnValue()) {
        out << "Task 5 finished successfully" << std::endl;
    }

    // if a task is not called to execute for whatever reason (specifically, the destructor is called before
    // the Task is executed), the TaskResult will be a TaskCancellation
    FutureTaskResult sixthTaskFuture = Task::create([]() { assert(false && "task is not called to execute"); }).second;

    if (const TaskCancellation *cancellation = sixthTaskFuture.getResult().getCancellation()) {
        out << "Task 6 was cancelled and did not execute" << std::endl;
    } else assert(false);

    // close queue -- lets workers finish once all tasks previously added are finished
    // any additional tasks added will be cancelled
    queue.close();
    worker::sync(queue);
    queue.getScheduler()->wait();
}
