#ifndef TASKMANAGER_TASK_HPP
#define TASKMANAGER_TASK_HPP

#include <variant>
#include <string>
#include <future>
#include <functional>
#include <optional>
#include <cassert>

namespace tskm {
    class Worker;

    namespace detail {
        std::optional<std::string> getExceptionMessage(const std::exception_ptr *exception);
    }

    class TaskCancellation {
    };

    template<typename R>
    class TypedTaskResult {
    private:
        std::variant<R, TaskCancellation, std::exception_ptr> variant;

        template<unsigned index>
        [[nodiscard]] auto get() const {
            return variant.index() == index ? &std::get<index>(variant) : nullptr;
        }

    public:
        TypedTaskResult(R value) : variant(std::move(value)) { // NOLINT(google-explicit-constructor)
        }

        // msvc seems to require future value to have no-args constructor
        TypedTaskResult(TaskCancellation c = {}) : variant(c) { // NOLINT(google-explicit-constructor)
        }

        TypedTaskResult(std::exception_ptr ptr) : variant(std::move(ptr)) { // NOLINT(google-explicit-constructor)
        }

        TypedTaskResult(const TypedTaskResult &) = default;
        TypedTaskResult &operator=(const TypedTaskResult &) = default;
        TypedTaskResult(TypedTaskResult &&) noexcept = default;
        TypedTaskResult &operator=(TypedTaskResult &&) noexcept = default;

        [[nodiscard]] bool success() const {
            return variant.index() == 0;
        }

        [[nodiscard]] const R *getReturnValue() const {
            return get<0>();
        }

        [[nodiscard]] const TaskCancellation *getCancellation() const {
            return get<1>();
        }

        [[nodiscard]] const std::exception_ptr *getException() const {
            return get<2>();
        }

        [[nodiscard]] std::optional<std::string> getExceptionMessage() const {
            return detail::getExceptionMessage(getException());
        }
    };

    template<>
    class TypedTaskResult<void> {
    public:
        static const TypedTaskResult<void> SUCCESS;
    private:
        std::variant<std::monostate, TaskCancellation, std::exception_ptr> variant;

        template<unsigned index>
        [[nodiscard]] auto get() const {
            return variant.index() == index ? &std::get<index>(variant) : nullptr;
        }

        TypedTaskResult(std::monostate s) : variant(s) { // NOLINT(google-explicit-constructor)
        }

    public:
        TypedTaskResult(TaskCancellation c = {}) : variant(c) { // NOLINT(google-explicit-constructor)
        }

        TypedTaskResult(std::exception_ptr ptr) : variant(std::move(ptr)) { // NOLINT(google-explicit-constructor)
        }

        [[nodiscard]] bool success() const {
            return variant.index() == 0;
        }

        [[nodiscard]] const TaskCancellation *getCancellation() const {
            return get<1>();
        }

        [[nodiscard]] const std::exception_ptr *getException() const {
            return get<2>();
        }

        [[nodiscard]] std::optional<std::string> getExceptionMessage() const {
            return detail::getExceptionMessage(getException());
        }
    };

    template<typename R>
    class TypedFutureTaskResult {
        friend class Task;
    private:
        std::shared_future<TypedTaskResult<R>> future;

        explicit TypedFutureTaskResult(std::shared_future<TypedTaskResult<R>> future) : future(std::move(future)) {}

    public:
        std::shared_future<TypedTaskResult<R>> *operator->() const {
            return future;
        }

        void wait() {
            future.wait();
        }

        [[nodiscard]] const R *getReturnValue() const {
            return future.get().getReturnValue();
        }

        [[nodiscard]] bool success() const {
            return future.get().success();
        }

        const TypedTaskResult<R> &getResult() const {
            return future.get();
        }
    };

    template<>
    class TypedFutureTaskResult<void> {
        friend class Task;
    private:
        std::shared_future<TypedTaskResult<void>> future;

        explicit TypedFutureTaskResult(std::shared_future<TypedTaskResult<void>> future) : future(std::move(future)) {}

    public:
        void wait() {
            future.wait();
        }

        [[nodiscard]] bool success() const {
            return future.get().success();
        }

        [[nodiscard]] const TypedTaskResult<void> &getResult() const {
            return future.get();
        }
    };

    // flag type (used to distinguish between unknown return type and actual void * return type
    struct IndeterminateReturnType {
        ~IndeterminateReturnType() = delete;
    };

    template<>
    class TypedTaskResult<IndeterminateReturnType> {
    private:
        struct Base {
            virtual ~Base() = default;

            virtual std::unique_ptr<Base> copy() = 0;
            [[nodiscard]] virtual const void *getReturnValue() const = 0;
            [[nodiscard]] virtual const std::exception_ptr *getException() const = 0;
            [[nodiscard]] virtual const TaskCancellation *getCancellation() const = 0;
        };

        template<typename R>
        struct TaskResult : public Base {
            static_assert(!std::is_same_v<R, IndeterminateReturnType>, "TaskResult cannot wrap Indeterminate type");

            const TypedTaskResult<R> &result;

            explicit TaskResult(const TypedTaskResult<R> &result) : result(result) {}

            std::unique_ptr<Base> copy() override {
                return std::make_unique<TaskResult>(result);
            }

            [[nodiscard]] const void *getReturnValue() const override {
                if constexpr (std::is_same_v<R, void>) {
                    return result.success() ? &TypedTaskResult<void>::SUCCESS : nullptr;
                } else {
                    return result.getReturnValue();
                }
            }

            [[nodiscard]] const std::exception_ptr *getException() const override {
                return result.getException();
            }

            [[nodiscard]] const TaskCancellation *getCancellation() const override {
                return result.getCancellation();
            }
        };

        std::unique_ptr<Base> base;

    public:
        template<typename R>
        TypedTaskResult(const TypedTaskResult<R> &result) : base(std::make_unique<TaskResult<R>>(result)) { // NOLINT(google-explicit-constructor)
        }

        TypedTaskResult(const TypedTaskResult &other) : base(other.base->copy()) {
        }

        TypedTaskResult &operator=(const TypedTaskResult &other) {
            base = std::move(other.base->copy());
            return *this;
        }

        TypedTaskResult(TypedTaskResult &&) noexcept = default;
        TypedTaskResult &operator=(TypedTaskResult &&) noexcept = default;

        [[nodiscard]] bool success() const {
            return base->getReturnValue();
        }

        [[nodiscard]] const void *getReturnValue() const {
            return base->getReturnValue();
        }

        [[nodiscard]] const std::exception_ptr *getException() const {
            return base->getException();
        }

        [[nodiscard]] std::optional<std::string> getExceptionMessage() const {
            return detail::getExceptionMessage(getException());
        }

        [[nodiscard]] const TaskCancellation *getCancellation() const {
            return base->getCancellation();
        }
    };

    using TaskResult = TypedTaskResult<IndeterminateReturnType>;

    template<>
    class TypedFutureTaskResult<IndeterminateReturnType> {
    private:
        struct Base {
            virtual ~Base() = default;

            virtual void wait() = 0;
            virtual TaskResult get() = 0;
        };

        template<typename R>
        struct Value : public Base {
            TypedFutureTaskResult<R> result;

            explicit Value(TypedFutureTaskResult<R> result) : result(std::move(result)) {
            }

            void wait() override {
                result.wait();
            }

            TaskResult get() override {
                return result.getResult();
            }
        };

        std::unique_ptr<Base> base;

    public:
        template<typename R>
        TypedFutureTaskResult(TypedFutureTaskResult<R> typed) : base(std::make_unique<Value<R>>(std::move(typed))) { // NOLINT(google-explicit-constructor)
        }

        [[nodiscard]] const void *getReturnValue() const {
            return base->get().getReturnValue();
        }

        [[nodiscard]] TaskResult getResult() const {
            return base->get();
        }
    };

    using FutureTaskResult = TypedFutureTaskResult<IndeterminateReturnType>;

    class Task {
        friend class Worker;
    private:
        struct Base {
            virtual ~Base() = default;
            virtual FutureTaskResult getFuture() = 0;
            virtual void cancel() noexcept = 0;
            virtual void launch() noexcept = 0;

#ifndef NDEBUG
            virtual void assertInactive() = 0;
#endif
        };

        template<typename R, typename ...Args>
        struct PackagedTask : public Base {
            using Function = std::function<R(Args...)>;

            std::promise<TypedTaskResult<R>> promise;
            TypedFutureTaskResult<R> future;
            Function function;
            std::tuple<Args...> args;
#ifdef NDEBUG
            bool
#else
            std::atomic_bool ended = false;
            std::atomic_bool
#endif
            // atomic if debug
            started = false;

            explicit PackagedTask(Function function, Args &&...args) :
                    function(std::move(function)), args(std::forward<Args>(args)...), future(promise.get_future()) {
                assert(this->function && "function must not be nullptr");
            }

            FutureTaskResult getFuture() override {
                return future;
            }

            void cancel() noexcept override {
                if (!started) {
                    started = true;
                    promise.set_value(TaskCancellation());
                }
            }

#ifndef NDEBUG

            void assertInactive() override {
                assert(started == ended && "task destructor cannot be called while task is running");
            }

#endif

            void launch() noexcept override {
                assert(!started && "task has already been launched");
                try {
                    started = true;
                    if constexpr(std::is_same_v<void, R>) {
                        std::apply(std::move(function), std::move(args));
                        promise.set_value(TypedTaskResult<void>::SUCCESS);
                    } else promise.set_value(std::apply(std::move(function), std::move(args)));
                } catch (const TaskCancellation &c) {
                    promise.set_value(c);
                } catch (...) {
                    promise.set_value(std::current_exception());
                }
#ifndef NDEBUG
                ended = true;
#endif
            }
        };

        std::unique_ptr<Base> base;

        explicit Task(std::unique_ptr<Base> base) : base(std::move(base)) {
        }

//        template<typename A, typename ...Args>
//        struct ArgPack {
//            constexpr static unsigned REMAINING = sizeof...(Args);
//            using Arg = A;
//            using NEXT = ArgPack<Args...>;
//
//            constexpr ArgPack() = default;
//        };
//
//        template<typename A>
//        struct ArgPack<A> {
//            constexpr static unsigned REMAINING = 0;
//            using Arg = A;
//        };
//
//        template<unsigned argIndex, typename ...FunctionArgs, typename ...Args>
//        static constexpr void checkArg(ArgPack<FunctionArgs...> functionArgs, ArgPack<Args...> args) {
//            using FunctionPack = ArgPack<FunctionArgs...>;
//            using ArgPack = ArgPack<Args...>;
//            static_assert(std::is_convertible_v<typename ArgPack::Arg, typename FunctionPack::Arg>,
//                    // todo - print more helpful information -- arg index, Arg type, FunctionParameter type
//                          "given argument is not implicitly convertable to function parameter");
//
//            // todo - add support for references (as long as Arg is not rvalue-reference)
//            static_assert(!std::is_reference_v<typename FunctionPack::Arg>, "function cannot accept reference arguments");
//
//            if constexpr(ArgPack::REMAINING) {
//                checkArg<argIndex + 1>(typename FunctionPack::NEXT(), typename ArgPack::NEXT());
//            }
//        }
    public:
        template<typename Functor, typename ...Args>
        static auto create(Functor functor, Args &&...args) {
            // std::pair<Task, TypedFutureTaskResult<R>>
            // check to make sure each arg is convertable to each function parameter
//            static_assert(!(sizeof...(FunctionParameters) > sizeof...(Args)), "task is missing function parameters");
//            static_assert(!(sizeof...(FunctionParameters) < sizeof...(Args)), "task given extra function parameters");
//            if constexpr (sizeof...(FunctionParameters) != 0) {
//                checkArg<0>(ArgPack<FunctionParameters...>(), ArgPack<Args...>());
//            }
//            auto pointer = std::make_unique<PackagedTask<R, FunctionParameters...>>(std::move(function), std::forward<Args>(args)...);

            static_assert(std::is_invocable_v<Functor, Args...>, "functor is not callable with given arguments");
            using R = std::invoke_result_t<Functor, Args...>;
            auto pointer = std::make_unique<PackagedTask<R, Args...>>(std::move(functor), std::forward<Args>(args)...);
            auto result = pointer->future;
            return std::make_pair(Task(std::move(pointer)), std::move(result));
        }

        Task(Task &&) noexcept = default;
        Task &operator=(Task &&) noexcept = default;

        // if task hasn't already executed, give future cancelled result
        ~Task() {
            if (base) {
#ifndef NDEBUG
                base->assertInactive();
#endif
                base->cancel();
            }
        }

        void operator()() noexcept {
            assert(base != nullptr && "cannot launch task: task has been moved");
            base->launch();
        }

        [[nodiscard]] FutureTaskResult getFuture() const {
            assert(base != nullptr && "cannot get task future: task has been moved");
            return base->getFuture();
        }
    };
}

#endif //TASKMANAGER_TASK_HPP
