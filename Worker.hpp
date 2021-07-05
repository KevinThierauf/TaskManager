#ifndef TASKMANAGER_WORKER_HPP
#define TASKMANAGER_WORKER_HPP

#include <memory>
#include <utility>
#include <mutex>

namespace tskm {
    class Worker;
    class Task;

    template<typename>
    class TaskSchedulerBase;
    using TaskScheduler = TaskSchedulerBase<Worker>;

    class DelayedStart {
        friend class Worker;
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;

        void startImpl();
        void accept(Worker *worker);
    public:
        DelayedStart();
        DelayedStart(const DelayedStart &) = delete;
        DelayedStart operator=(const DelayedStart &) = delete;
        // allowing movement makes all kinds of trouble for synchronization
        DelayedStart(DelayedStart &&) noexcept = delete;
        DelayedStart operator=(DelayedStart &&) noexcept = delete;

        ~DelayedStart();

        void start();
        [[nodiscard]] bool isEmpty() const;
    };

    class Worker {
        friend class DelayedStart;
    public:
        enum class State {
            TERMINATE,
            WAIT,
            WORK
        };

        // problems with calling constructor/destructor can be resolved by adding a level of
        // indirection
        // functions are noexcept since they may be called in various constructors/destructors
        // functions may be called concurrently -- implementors must make functions thread-safe
        struct Impl {
            // so that subclasses don't have reference state through Worker::
            using State = State;

            virtual ~Impl() = default;

            // start worker
            virtual void start(const Worker &) noexcept = 0;
            // request a state
            virtual void requestState(State state) noexcept = 0;
            // the current state
            [[nodiscard]] virtual State getState() const noexcept = 0;
            // the requested state
            [[nodiscard]] virtual State getRequestedState() const noexcept = 0;
            // wait until worker enters terminated state
            virtual void wait() const noexcept = 0;
        };
    private:
        std::unique_ptr<Impl> impl;

        void start() noexcept {
            impl->start(*this);
        }

    public:
        explicit Worker(std::unique_ptr<Impl> impl, DelayedStart *startDelay = nullptr);
        Worker(const Worker &) = delete;
        Worker &operator=(const Worker &) = delete;
        Worker(Worker &&) noexcept = delete;
        Worker &operator=(Worker &&) noexcept = delete;
        ~Worker();

        void requestState(State state) noexcept {
            impl->requestState(state);
        }

        [[nodiscard]] State getRequestedState() const noexcept {
            return impl->getRequestedState();
        }

        [[nodiscard]] State getState() const noexcept {
            return impl->getState();
        }

        void wait() noexcept {
            impl->wait();
        }
    };

    namespace worker {
        std::unique_ptr<Worker> sync(std::shared_ptr<TaskScheduler> scheduler, DelayedStart *startDelay = nullptr);
        std::unique_ptr<Worker> async(std::shared_ptr<TaskScheduler> scheduler, DelayedStart *startDelay = nullptr);
        // todo - child process
        // todo - networked process
    }
}

#endif //TASKMANAGER_WORKER_HPP
