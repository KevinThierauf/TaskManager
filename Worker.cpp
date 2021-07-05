#include "Worker.hpp"
#include "Task.hpp"
#include "TaskScheduler.hpp"
#include <mutex>
#include <cassert>

namespace tskm {
    struct DelayedStart::Impl {
        std::mutex mutex;
        Worker *worker = nullptr;
    };

    DelayedStart::DelayedStart() : impl(std::make_unique<Impl>()) {
    }

    DelayedStart::~DelayedStart() {
        start();
    }

    void DelayedStart::startImpl() {
        if (impl->worker) {
            auto worker = impl->worker;
            impl->worker = nullptr;
            worker->start();
        }
    }

    void DelayedStart::accept(Worker *worker) {
        std::scoped_lock lock(impl->mutex);
        startImpl(); // if already contains Worker, start the previous before accepting
        impl->worker = worker;
    }

    void DelayedStart::start() {
        std::scoped_lock lock(impl->mutex);
        startImpl();
    }

    bool DelayedStart::isEmpty() const {
        std::scoped_lock lock(impl->mutex);
        return !impl->worker;
    }

    Worker::Worker(std::unique_ptr<Impl> impl, DelayedStart *startDelay) : impl(std::move(impl)) {
        assert(this->impl && "impl is nullptr");
        if (startDelay) startDelay->accept(this);
        else start();
    }

    Worker::~Worker() {
        assert(impl->getState() == State::TERMINATE && "worker destroyed before finished");
    }

    namespace worker {
        namespace {
            class SyncedWorker : public Worker::Impl {
            private:
                std::shared_ptr<TaskScheduler> scheduler;
                State currentState = State::WAIT;
                State requestedState = State::WORK;
                mutable std::condition_variable waitVariable; // notify when no longer waiting
            protected:
                mutable std::mutex mutex;
            public:
                explicit SyncedWorker(std::shared_ptr<TaskScheduler> scheduler) : scheduler(std::move(scheduler)) {}

                void requestState(State state) noexcept override {
                    bool notifyWait = false;

                    {
                        std::scoped_lock lock(mutex);
                        assert(requestedState == State::TERMINATE || state != State::TERMINATE && "state cannot leave terminated state");
                        if (requestedState != state) {
                            if (requestedState == State::WAIT) {
                                // state is != requestedState, aka !wait
                                notifyWait = true;
                            }
                            requestedState = state;
                        }
                    }

                    // release lock before notifying (not strictly required, but prevents unneeded blocking)
                    if (notifyWait) waitVariable.notify_all();
                }

                State getState() const noexcept override {
                    std::scoped_lock lock(mutex);
                    return currentState;
                }

                State getRequestedState() const noexcept override {
                    std::scoped_lock lock(mutex);
                    return requestedState;
                }

                void runLoop(std::unique_lock<std::mutex> &lock) {
                    while (true) {
                        currentState = requestedState;

                        if (requestedState == State::TERMINATE) return;

                        while (requestedState == State::WAIT) {
                            if (scheduler->isCompleted()) return;
                            waitVariable.wait(lock);
                        }

                        if (requestedState == State::WORK) {
                            if (scheduler->isCompleted()) return;
                            // release lock for duration of task; reacquire afterwards
                            detail::Unlock l(lock);
                            scheduler->startScheduledWork([&](bool) {
                                std::scoped_lock predicateLock(mutex);
                                return requestedState == State::WORK;
                            });
                        }
                    }
                }

                void start(const Worker &) noexcept override {
                    {
                        std::unique_lock lock(mutex);
                        runLoop(lock);
                        currentState = State::TERMINATE;
                    }
                    waitVariable.notify_all();
                }

                void wait() const noexcept override {
                    scheduler->wait();
                    waitVariable.notify_all();

                    std::unique_lock lock(mutex);
                    while(currentState != Worker::State::TERMINATE) {
                        waitVariable.wait(lock);
                    }
                }
            };

            class AsyncWorker : public SyncedWorker {
            private:
                std::thread thread;
            public:
                explicit AsyncWorker(std::shared_ptr<TaskScheduler> scheduler) : SyncedWorker(std::move(scheduler)) {}

                ~AsyncWorker() override {
                    assert(thread.joinable() && "async worker created but not started");
                    thread.join();
                }

                void start(const Worker &worker) noexcept override {
                    thread = std::thread([&worker(worker), this]() {
                        SyncedWorker::start(worker);
                    });
                }
            };
        }

        std::unique_ptr<Worker> sync(std::shared_ptr<TaskScheduler> scheduler, DelayedStart *startDelay) {
            return std::make_unique<Worker>(std::make_unique<SyncedWorker>(std::move(scheduler)), startDelay);
        }

        std::unique_ptr<Worker> async(std::shared_ptr<TaskScheduler> scheduler, DelayedStart *startDelay) {
            return std::make_unique<Worker>(std::make_unique<AsyncWorker>(std::move(scheduler)), startDelay);
        }
    }
}