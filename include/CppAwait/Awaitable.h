/*
* Copyright 2012-2013 Valentin Milea
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/**
 * @file  Awaitable.h
 *
 * Declares the Awaitable class and related helpers.
 *
 */

#pragma once

#include "Config.h"
#include "Coro.h"
#include "impl/Assert.h"
#include <memory>
#include <array>

namespace ut {

class Awaitable;
struct AwaitableImpl;

namespace detail
{
    template <typename F>
    class CallbackWrapper;
}

/**
 * Handle for completing an Awaitable
 *
 * Completer is copyable. The first Completer to complete() / fail()
 * the Awaitable wins, and the rest expire.
 */
class Completer
{
public:
    /** Construct a dummy completer */
    Completer() { }

    /** Copy constructor */
    Completer(const Completer& other)
        : mRef(other.mRef) { }

    /** Copy assignment */
    Completer& operator=(const Completer& other)
    {
        if (this != &other) {
            mRef = other.mRef;
        }

        return *this;
    }

    /** Move constructor */
    Completer(Completer&& other)
        : mRef(std::move(other.mRef)) { }

    /** Move assignment */
    Completer& operator=(Completer&& other)
    {
        mRef = std::move(other.mRef);

        return *this;
    }

    /** Check if awaitable is done */
    bool isExpired() const
    {
        return mRef.expired();
    }

    /** Returns associated awaitable if not expired, nullptr otherwise. */
    Awaitable* awaitable() const;

    /** Calls complete() */
    void operator()() const
    {
        complete();
    }

    /**
     * Complete awaitable; resumes awaiting coroutine
     *
     * Must be called from master coroutine. Does nothing if expired.
     */
    void complete() const;

    /**
     * Fail awaitable; throws exception on awaiting coroutine
     *
     * Must be called from master coroutine. Does nothing if expired.
     */
    void fail(std::exception_ptr eptr) const;

    /**
     * Wraps a callback function
     *
     * The wrapper executes func and immediately finishes Awaitable. Nothing
     * happens if the wrapper runs after Awaitable is done (and possibly
     * destroyed).
     *
     * @param  func  callback to wrap. Must not throw. Must return a std::exception_ptr.
     *               The empty exception_ptr triggers complete(), any other return triggers fail().
     * @return wrapped func
     */
    template <typename F>
    detail::CallbackWrapper<F> wrap(F func) const
    {
        return detail::CallbackWrapper<F>(*this, std::move(func));
    }

private:
    explicit Completer(const std::shared_ptr<void *>& ref)
        : mRef(ref) { }

    std::weak_ptr<void *> mRef;

    friend class Awaitable;
};

//
// Awaitable
//

/**
 * Wrapper for asynchronous operations
 *
 * Awaitable is an a abstraction for asynchronous operations. It represents a
 * unit of work that is expected to finish at some time in the future.
 *
 * While inside a couroutine it's possible to _await_ for some Awaitable to finish. In
 * the coroutine, await() appears to block until the Awaitable completes or fails.
 * Actually the coroutine is suspended and yields control immediately to the program's
 * main loop, allowing for other work to be done while the asynchronous operation is
 * running.
 *
 * An awaitable operation may be implemented as a coroutine (usually when composing
 * simpler awaitables). Or the Awaitable could be hooked to some task running on an
 * external thread.
 *
 * The Awaitable owns its asynchronous operation. Destroying it must immediately
 * interrupt the operation.
 *
 * @warning Not thread safe. Awaitables are designed for single-threaded use.
 *
 */
class Awaitable
{
public:
    /**
     * Pointer to implementation
     *
     * Useful when you need a lightweight reference that is not invalidated
     * after std::move(Awaitable), but don't want to carry a Completer.
     */
    struct Pointer
    {
        AwaitableImpl *m;

        Pointer(AwaitableImpl *m)
            : m(m) { }

        Awaitable* operator->() const
        {
            return get();
        }

        Awaitable& operator*() const
        {
            return *get();
        }

        Awaitable* get() const;
    };

    /** Create an awaitable this way if you intend to take its Completer */
    explicit Awaitable(std::string tag = std::string());

    ~Awaitable();

    /**
     * Move constructor
     *
     * After move, 'other' may only be destructed or assigned to. Anything else
     * is undefined behavior.
     */
    Awaitable(Awaitable&& other);

    /**
     * Move assignment
     *
     * After move, 'other' may only be destructed or assigned to. Anything else
     * is undefined behavior.
     */
    Awaitable& operator=(Awaitable&& other);

    /**
     * Suspend current coroutine until done
     *
     * If not yet done, await() yields control to master coroutine. As an optimization,
     * if the Awaitable was created with startAsync() and it has not yet started,
     * control will be yielded directly to its coroutine instead.
     *
     * On successful completion the awaiting coroutine is resumed. Subsequent
     * calls to await() will return immediately.
     * On failure the exception will be raised in the awaiting coroutine (if any).
     * Each subsequent await() will raise the exception again.
     *
     * Note:
     * - must be called from a coroutine (not from main stack)
     * - awaiting from several coroutines at the same time is not supported
     */
    void await();

    /* True if operation has completed successfully */
    bool didComplete();

    /** True if operation has failed */
    bool didFail();

    /** True if completed or failed */
    bool isDone();

    /** Exception set on fail */
    std::exception_ptr exception();

    /** Add a custom handler to be called when done */
    void then(ut::Action slot);

    /** Take the completer functor */
    Completer takeCompleter();

    /** True if completer not yet taken */
    bool isNil();

    /** Identifier for debugging */
    const char* tag();

    /** Sets an identifier for debugging */
    void setTag(std::string tag);

    /** Returns implementation pointer */
    Pointer pointer();

    /** Shorthand for takeCompleter().wrap(std::move(func)) */
    template <typename F>
    detail::CallbackWrapper<F> wrap(F func)
    {
        return detail::CallbackWrapper<F>(takeCompleter(), std::move(func));
    }

    /**
     * Explicitly set continuation coroutine. Enables awaitAny (select / poll) pattern.
     *
     * @param coro   coroutine to yield to after complete() / fail()
     */
    void setAwaitingCoro(Coro *coro);

    /** Returns a completed awaitable. This is more efficient than taking and immediately calling completer. */
    static Awaitable makeCompleted();

    /** Returns a failed awaitable. This is more efficient than taking and immediately calling completer. */
    static Awaitable makeFailed(std::exception_ptr eptr);

private:
    Awaitable(const Awaitable&); // noncopyable
    Awaitable& operator=(const Awaitable&); // noncopyable

    void complete();

    void fail(std::exception_ptr eptr);

    void clear();

    AwaitableImpl *m;

    template <typename Collection>
    friend typename Collection::iterator awaitAny(Collection& awaitables);

    friend class Completer;
    friend Awaitable startAsync(std::string tag, Action func, size_t stackSize);
};


//
// helpers
//

/**
 * Schedules a function to run asynchronously
 * @param   tag        awaitable tag
 * @param   func       coroutine function
 * @param   stackSize  size of stack to allocate for coroutine
 * @return  an awaitable for managing the asyncronous operation
 *
 * This function prepares func to run as a coroutine. It allocates a Coro and
 * returns an Awaitable hooked up to the coroutine. By using startAsync() you
 * never need to deal directly with Coro.
 *
 * Uncaught exceptions from func -- except for ForcedUnwind -- will pop out on
 * the awaiting coroutine.
 *
 * If you delete the Awaitable while the func is running (i.e. while it is awaiting
 * some suboperation), the coroutine will resume with a ForcedUnwind exception.
 * It's expected func will exit immediately upon ForcedUnwind, so make sure not to
 * ignore it in a catch(...) block.
 *
 * Actions created this way have their completer already taken.
 */
Awaitable startAsync(std::string tag, Action func, size_t stackSize = Coro::defaultStackSize());


/**
 * @name Awaitable selectors
 *
 * Attribute shims that are used by awaitAll() / awaitAny() to extract Awaitables
 * from Collection. You can define your own overloads.
 */
///@{

/** select Awaitable from a pointer to Awaitable */
inline Awaitable* selectAwaitable(Awaitable* element)
{
    return element;
}

/** select Awaitable from a reference to Awaitable */
inline Awaitable* selectAwaitable(Awaitable& element)
{
    return &element;
}

/** select Awaitable from a unique_ptr<Awaitable> */
inline Awaitable* selectAwaitable(std::unique_ptr<Awaitable>& element)
{
    return element.get();
}

/** select Awaitable from a std::pair */
template <typename First, typename Second>
Awaitable* selectAwaitable(std::pair<First, Second>& element)
{
    return selectAwaitable(element.first);
}

/** select Awaitable from any structure with a field named awaitable */
template <typename T>
Awaitable* selectAwaitable(T& element)
{
    return selectAwaitable(element.awaitable);
}

///@}

// combinators

/**
 * Yield until all awaitables have completed or one of them fails
 * @param awaitables  a collection from which awaitables can be selected
 *
 * Equivalent to calling await() in sequence for each member of the collection.
 * If any awaitable fails the exception propagates to caller.
 */
template <typename Collection>
void awaitAll(Collection& awaitables)
{
    ut_assert_(currentCoro() != masterCoro());

    for (auto it = awaitables.begin(); it != awaitables.end(); ++it) {
        Awaitable *awt = selectAwaitable(*it);
        if (awt == nullptr) {
            continue;
        }

        awt->await();
    }
}

/**
 * Yield until any of the awaitables has completed or failed
 * @param awaitables  a collection from which awaitables can be selected
 * @return  iterator to the first awaitable that is done
 *
 * Note: If an awaitable fails, the exception is not propagated. You can
 *       trigger it explicitly by awaiting on returned iterator.
 */
template <typename Collection>
typename Collection::iterator awaitAny(Collection& awaitables)
{
    ut_assert_(currentCoro() != masterCoro());

    bool havePendingAwts = false;

    for (auto it = awaitables.begin(); it != awaitables.end(); ++it) {
        Awaitable *awt = selectAwaitable(*it);
        if (awt == nullptr) {
            continue;
        }
        if (awt->isDone()) {
            return it;
        }
        havePendingAwts = true;
    }
    if (!havePendingAwts) {
        return awaitables.begin();
    }

    for (auto it = awaitables.begin(); it != awaitables.end(); ++it) {
        Awaitable *awt = selectAwaitable(*it);
        if (awt == nullptr) {
            continue;
        }
        awt->setAwaitingCoro(currentCoro());
    }

    yieldTo(masterCoro());

    auto completedPos = awaitables.end();
    Awaitable *completedAwt = nullptr;

    for (auto it = awaitables.begin(); it != awaitables.end(); ++it) {
        Awaitable *awt = selectAwaitable(*it);
        if (awt == nullptr) {
            continue;
        }
        awt->setAwaitingCoro(nullptr);

        if (completedAwt == nullptr && awt->isDone()) {
            completedAwt = awt;
            completedPos = it;
        }
    }

    ut_assert_(completedAwt != nullptr);
    ut_assert_(completedAwt->isDone());

    return completedPos;
}

/** Compose a collection of awaitables, awaits for all to complete */
template <typename Collection>
Awaitable asyncAll(Collection& awaitables)
{
    return startAsync("asyncAll", [&awaitables]() {
        awaitAll(awaitables);
    });
}

/** Compose a collection of awaitables, awaits for any to complete */
template <typename Collection>
Awaitable asyncAny(Collection& awaitables, typename Collection::iterator& pos)
{
    return startAsync("asyncAny", [&awaitables, &pos]() {
        if (awaitables.empty()) {
            yieldTo(masterCoro()); // never complete
        } else {
            pos = awaitAny(awaitables);
        }
    });
}

// convenience overloads

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable *awt1, Awaitable *awt2)
{
    ut_assert_(awt1 && awt2);

    std::array<Awaitable*, 2> awts = {{ awt1, awt2 }};
    awaitAll(awts);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3)
{
    ut_assert_(awt1 && awt2 && awt3);

    std::array<Awaitable*, 3> awts = {{ awt1, awt2, awt3 }};
    awaitAll(awts);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4);

    std::array<Awaitable*, 4> awts = {{ awt1, awt2, awt3, awt4 }};
    awaitAll(awts);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4, Awaitable *awt5)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4 && awt5);

    std::array<Awaitable*, 5> awts = {{ awt1, awt2, awt3, awt4, awt5 }};
    awaitAll(awts);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable& awt1, Awaitable& awt2)
{
    return awaitAll(&awt1, &awt2);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable& awt1, Awaitable& awt2, Awaitable& awt3)
{
    return awaitAll(&awt1, &awt2, &awt3);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable& awt1, Awaitable& awt2, Awaitable& awt3, Awaitable& awt4)
{
    return awaitAll(&awt1, &awt2, &awt3, &awt4);
}

/** Yield until all awaitables have completed or one of them fails */
inline void awaitAll(Awaitable& awt1, Awaitable& awt2, Awaitable& awt3, Awaitable& awt4, Awaitable& awt5)
{
    return awaitAll(&awt1, &awt2, &awt3, &awt4, &awt5);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2)
{
    ut_assert_(awt1 && awt2);

    std::array<Awaitable*, 2> awts = {{ awt1, awt2 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3)
{
    ut_assert_(awt1 && awt2 && awt3);

    std::array<Awaitable*, 3> awts = {{ awt1, awt2, awt3 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4);

    std::array<Awaitable*, 4> awts = {{ awt1, awt2, awt3, awt4 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable *awt1, Awaitable *awt2, Awaitable *awt3, Awaitable *awt4, Awaitable *awt5)
{
    ut_assert_(awt1 && awt2 && awt3 && awt4 && awt5);

    std::array<Awaitable*, 5> awts = {{ awt1, awt2, awt3, awt4, awt5 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable& awt1, Awaitable& awt2)
{
    std::array<Awaitable*, 2> awts = {{ &awt1, &awt2 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable& awt1, Awaitable& awt2, Awaitable& awt3)
{
    std::array<Awaitable*, 3> awts = {{ &awt1, &awt2, &awt3 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable& awt1, Awaitable& awt2, Awaitable& awt3, Awaitable& awt4)
{
    std::array<Awaitable*, 4> awts = {{ &awt1, &awt2, &awt3, &awt4 }};
    return *awaitAny(awts);
}

/** Yield until any of the awaitables has completed or failed */
inline Awaitable* awaitAny(Awaitable& awt1, Awaitable& awt2, Awaitable& awt3, Awaitable& awt4, Awaitable& awt5)
{
    std::array<Awaitable*, 5> awts = {{ &awt1, &awt2, &awt3, &awt4, &awt5 }};
    return *awaitAny(awts);
}

//
// impl
//

namespace detail
{
    // Helps wrap asynchronous APIs by hooking the Completer to raw callback
    //
    template <typename F>
    class CallbackWrapper
    {
    public:
        CallbackWrapper(const Completer& completer, F&& callback)
            : mCompleter(completer)
            , mCallback(std::move(callback)) { }

        CallbackWrapper(Completer&& completer, F&& callback)
            : mCompleter(std::move(completer))
            , mCallback(std::move(callback)) { }

        CallbackWrapper(const CallbackWrapper<F>& other)
            : mCompleter(other.mCompleter)
            , mCallback(other.mCallback) { }

        CallbackWrapper<F>& operator=(const CallbackWrapper<F>& other)
        {
            if (this != &other) {
                mCompleter = other.mCompleter;
                mCallback = other.mCallback;
            }

            return *this;
        }

        CallbackWrapper(CallbackWrapper<F>&& other)
            : mCompleter(std::move(other.mCompleter))
            , mCallback(std::move(other.mCallback)) { }

        CallbackWrapper<F>& operator=(CallbackWrapper<F>&& other)
        {
            mCompleter = std::move(other.mCompleter);
            mCallback = std::move(other.mCallback);

            return *this;
        }

    #define UT_CALLBACK_WRAPPER_IMPL(...) \
        if (!mCompleter.isExpired()) { \
            std::exception_ptr eptr = mCallback(__VA_ARGS__); \
            \
            if (is(eptr)) { \
                mCompleter.fail(std::move(eptr)); \
            } else { \
                mCompleter(); \
            } \
        }

        void operator()()
        {
            UT_CALLBACK_WRAPPER_IMPL();
        }

        template <typename Arg1>
        void operator()(Arg1&& arg1)
        {
            UT_CALLBACK_WRAPPER_IMPL(std::forward<Arg1>(arg1));
        }

        template <typename Arg1, typename Arg2>
        void operator()(Arg1&& arg1, Arg2&& arg2)
        {
            UT_CALLBACK_WRAPPER_IMPL(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2));
        }

        template <typename Arg1, typename Arg2, typename Arg3>
        void operator()(Arg1&& arg1, Arg2&& arg2, Arg3&& arg3)
        {
            UT_CALLBACK_WRAPPER_IMPL(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Arg3>(arg3));
        }

        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
        void operator()(Arg1&& arg1, Arg2&& arg2, Arg3&& arg3, Arg4&& arg4)
        {
            UT_CALLBACK_WRAPPER_IMPL(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Arg3>(arg3),
                std::forward<Arg4>(arg4));
        }

        template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
        void operator()(Arg1&& arg1, Arg2&& arg2, Arg3&& arg3, Arg4&& arg4, Arg5&& arg5)
        {
            UT_CALLBACK_WRAPPER_IMPL(std::forward<Arg1>(arg1), std::forward<Arg2>(arg2), std::forward<Arg3>(arg3),
                std::forward<Arg4>(arg4), std::forward<Arg5>(arg5));
        }

    private:
        Completer mCompleter;
        F mCallback;
    };
}

}
