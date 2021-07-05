# TaskManager
TaskManager is a thread pool library designed to be:
 - easy to use: standard use cases should be as straightforward as possible, with more complicated uses requiring minimal additional effort. API should be consistent.
 - highly flexible: user should be able to customize any and all parts of the API to best fit their needs. Components should be modular and easily replaceable.
 - safe(ish). It's still C++.

It should be noted that while this implementation is intended to be efficient, runtime efficiency is secondary to the goals listed above. For the vast majority of users, this should not be a problem -- if, however, you aim to get the highest performance possible, it may be worth considering another thread pool library. At some point I plan on doing benchmarking to understand exactly how the performance compares.

You can check demo/main.cpp to see a fully working example. A simple use case may look something like the following:

    using namespace tskm;

    // create a basic task queue
    TaskQueue queue;

    // add some tasks
    queue.addTask([](unsigned num) {
        std::this_thread::sleep_for(std::chrono::milliseconds(num));
    }, 50);

    TypedFutureTaskResult<float> future = queue.addTask([]() {
        return 4.0f;
    });

    // create some (asyncrhonous) workers
    // workers will start automatically, unless otherwise requested
    auto worker1 = worker::async(queue);
    auto worker2 = worker::async(queue);

    // check task return value, check for error
    if (const float *returnValue = future.getResult().getReturnValue()) {
        std::cout << "future returned: " << *returnValue << std::endl;
    } else {
        // task threw an error, or was never called (should never happen in this example).
        std::cerr << "something went wrong!" << std::endl;
    }

    // finish up -- close queue to new tasks and wait for workers to finish
    queue.close(); // tasks can no longer be added to queue
    worker1->wait();
    worker2->wait();

Future goals of this library include:
 - run tasks in child process
 - run tasks in networked process