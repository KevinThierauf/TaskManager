#ifndef TASKMANAGER_TASKSCHEDULER_HPP
#define TASKMANAGER_TASKSCHEDULER_HPP

#include "Task.hpp"
#include <memory>
#include <mutex>
#include <condition_variable>

namespace tskm {
    class Worker;
    class Task;

    namespace detail {
        struct Unlock {
            std::unique_lock<std::mutex> &lock;

            explicit Unlock(std::unique_lock<std::mutex> &lock) : lock(lock) {
                lock.unlock();
            }

            ~Unlock() {
                lock.lock();
            }
        };
    }

    // schedules work to be performed
    // implementors must provide thread safety
    template<typename WorkerType = Worker>
    class TaskSchedulerBase {
    public:
        using Worker = WorkerType;
    private:
        bool completed = false;
        mutable std::condition_variable variable;

        std::atomic_uint16_t workers = 0;
    protected:
        mutable std::mutex mutex;

        void setCompleted(std::unique_lock<std::mutex> &lock) {
            completed = true;
            variable.notify_all();
        }

        [[nodiscard]] bool isCompleted(std::unique_lock<std::mutex> &lock) const {
            return completed;
        }

    public:
        virtual ~TaskSchedulerBase() = default;

        // bool parameter specifying whether or not there is work available
        // bool return value false to stop and return, and true to continue
        // if the predicate returns true, and the workAvailable parameter is
        // also true, the caller will start the work. if the workAvailable parameter
        // is false, the caller will block until checkWaitingPredicates() is called,
        // or work becomes available
        using Predicate = std::function<bool(bool workAvailable)>;

        virtual void startScheduledWork(const Predicate &predicate) = 0;
        // wakes up any waiting threads, and checks their predicates to see if should continue
        // this does nothing to threads that are currently running a task
        virtual void checkWaitingPredicates() = 0;

        [[nodiscard]] bool isCompleted() const {
            std::scoped_lock lock(mutex);
            return completed;
        }

        void wait() const {
            std::unique_lock lock(mutex);
            while (!completed) {
                variable.wait(lock);
            }
        }
    };

    using TaskScheduler = TaskSchedulerBase<>;

    // basic TaskScheduler wrapping a queue
    class TaskQueue {
    public:
        // the actual scheduler is held by a shared_ptr
        // in the event the TaskQueue is destroyed while workers are active, the workers will be able to finish the tasks
        // no extra tasks can be added after TaskQueue is destroyed
        class Scheduler : public TaskScheduler {
            friend class TaskQueue;
        private:
            struct Impl;
            std::unique_ptr<Impl> impl;

            void runNextTask(std::unique_lock<std::mutex> &lock);
            void checkCompleted(std::unique_lock<std::mutex> &lock);
        public:
            Scheduler();
            ~Scheduler() override;
        public:
            void startScheduledWork(const Predicate &predicate) override;
            void checkWaitingPredicates() override;
        };
    private:
        std::shared_ptr<Scheduler> consumer;
    public:
        TaskQueue();
        ~TaskQueue();

        operator std::shared_ptr<TaskScheduler>() const { // NOLINT(google-explicit-constructor)
            return consumer;
        }

        [[nodiscard]] std::shared_ptr<TaskScheduler> getScheduler() const {
            return consumer;
        }

        void addTask(Task task);

        template<typename Functor, typename ...Args>
        auto addTask(Functor functor, Args &&...args) {
            auto pair = Task::create(std::move(functor), std::forward<Args>(args)...);
            addTask(std::move(pair.first));
            return pair.second;
        }

        void cancel();
        void close();
        [[nodiscard]] bool isClosed() const;
    };
}

#endif //TASKMANAGER_TASKSCHEDULER_HPP
