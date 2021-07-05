#include "Task.hpp"
#include "TaskScheduler.hpp"
#include <mutex>
#include <queue>

namespace tskm {
    struct TaskQueue::Scheduler::Impl {
        std::condition_variable variable;
        std::queue<Task> queue;
        bool queueActive = true; // whether additional tasks can be added to the queue
        unsigned working = 0; // number of current workers
    };

    TaskQueue::Scheduler::Scheduler() : impl(std::make_unique<Impl>()) {
    }

    TaskQueue::Scheduler::~Scheduler() = default;

    void TaskQueue::Scheduler::runNextTask(std::unique_lock<std::mutex> &lock) {
        Task task = std::move(impl->queue.front());
        impl->queue.pop();
        {
            detail::Unlock unlock(lock);
            task();
        }
        checkCompleted(lock);
    }

    void TaskQueue::Scheduler::startScheduledWork(const Predicate &predicate) {
        std::unique_lock lock(mutex);
        while (true) {
            bool workReady = !impl->queue.empty();
            if (isCompleted(lock) || !predicate(workReady)) break;
            if (workReady) {
                impl->working++;
                runNextTask(lock);
                impl->working--;
                checkCompleted(lock);
            } else impl->variable.wait(lock);
        }
    }

    void TaskQueue::Scheduler::checkWaitingPredicates() {
        impl->variable.notify_all();
    }

    void TaskQueue::Scheduler::checkCompleted(std::unique_lock<std::mutex> &lock) {
        if (impl->working == 0 && impl->queue.empty() && !impl->queueActive) {
            setCompleted(lock);
            checkWaitingPredicates();
        }
    }

    TaskQueue::TaskQueue() : consumer(std::make_shared<Scheduler>()) {
    }

    TaskQueue::~TaskQueue() {
        std::unique_lock lock(consumer->mutex);
        consumer->impl->queueActive = false;
        consumer->checkCompleted(lock);
    }

    void TaskQueue::addTask(Task task) {
        std::scoped_lock lock(consumer->mutex);
        if(consumer->impl->queueActive) {
            consumer->impl->queue.push(std::move(task));
            consumer->impl->variable.notify_one();
        }
    }

    void TaskQueue::cancel() {
        std::unique_lock lock(consumer->mutex);
        std::queue<Task>().swap(consumer->impl->queue);
        consumer->checkCompleted(lock);
    }

    void TaskQueue::close() {
        std::unique_lock lock(consumer->mutex);
        consumer->impl->queueActive = false;
        consumer->checkCompleted(lock);
    }

    bool TaskQueue::isClosed() const {
        std::unique_lock lock(consumer->mutex);
        return !consumer->impl->queueActive;
    }
}