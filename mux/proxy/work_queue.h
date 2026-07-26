#ifndef HYDRA_WORK_QUEUE_H
#define HYDRA_WORK_QUEUE_H

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>

class SessionManager;
class AccountManager;
struct HydraConfig;
class ProcessManager;

// A unit of work posted from a gRPC thread to the main event loop.
// The execute() method runs in the main thread with full access to
// all Hydra subsystems.
class WorkItem {
public:
    virtual ~WorkItem() = default;
    virtual void execute(SessionManager& sessionMgr,
                         AccountManager& accounts,
                         const HydraConfig& config,
                         ProcessManager& procMgr) = 0;
};

// Type-erased work item that wraps a callable + promise.
// gRPC handlers create one of these, enqueue it, then wait on the future.
template<typename Result>
class TypedWorkItem : public WorkItem {
public:
    using Func = std::function<Result(SessionManager&, AccountManager&,
                                      const HydraConfig&, ProcessManager&)>;

    explicit TypedWorkItem(Func fn) : fn_(std::move(fn)) {}

    std::future<Result> getFuture() { return promise_.get_future(); }

    void execute(SessionManager& sessionMgr, AccountManager& accounts,
                 const HydraConfig& config, ProcessManager& procMgr) override {
        try {
            if constexpr (std::is_void_v<Result>) {
                fn_(sessionMgr, accounts, config, procMgr);
                promise_.set_value();
            } else {
                promise_.set_value(fn_(sessionMgr, accounts, config, procMgr));
            }
        } catch (...) {
            promise_.set_exception(std::current_exception());
        }
    }

    // Complete the promise without running fn_, for work that will never
    // reach the main loop because the queue has stopped (#1286).
    //
    // A value-initialised Result rather than a broken promise: every
    // future.get() in grpc_server.cpp is unguarded, so breaking the promise
    // would throw out of a gRPC handler.  Each handler already reads the
    // default as failure -- bool -> false, std::string -> "" (empty pid ==
    // auth failed), pair -> {"",""}, shared_ptr<OutputQueue> -> nullptr,
    // which its three call sites already test with `if (!oq) return
    // NOT_FOUND`.  A client mid-shutdown gets a clean denial.
    //
    void cancel() {
        if constexpr (std::is_void_v<Result>) {
            promise_.set_value();
        } else {
            promise_.set_value(Result{});
        }
    }

private:
    Func fn_;
    std::promise<Result> promise_;
};

// Thread-safe work queue. gRPC threads enqueue, main loop processes.
//
// #1265: bounded pending depth so a fast GameSession reader cannot grow
// host RAM without limit.  Enqueue blocks until space is available (natural
// backpressure on the gRPC thread).
class WorkQueue {
public:
    // High-water mark on pending items.  Chosen large enough for a burst of
    // unary RPCs under load, small enough to bound memory (each item holds a
    // captured input line up to MAX_LINE_LENGTH).
    static constexpr size_t MAX_PENDING = 1024;

    // Enqueue a work item. Returns a future for the result.
    // Blocks while the queue is full (#1265).
    // Called from gRPC threads.
    template<typename Result>
    std::future<Result> enqueue(
        std::function<Result(SessionManager&, AccountManager&,
                             const HydraConfig&, ProcessManager&)> fn) {
        auto item = std::make_unique<TypedWorkItem<Result>>(std::move(fn));
        auto future = item->getFuture();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // `stopped_` releases the wait at shutdown (#1286).  Without
            // it, a producer parked here has no way out: cv_space_ is only
            // notified by processPending(), and after the main loop's final
            // drain nothing calls it again -- so ~GrpcServer's Shutdown(),
            // which waits for in-flight RPCs, would wait forever.
            //
            cv_space_.wait(lock, [this] {
                return queue_.size() < MAX_PENDING || stopped_;
            });
            if (stopped_) {
                // Complete the item here rather than pushing it.  Pushing
                // relies on either a later drain or ~WorkQueue breaking the
                // promise, and neither happens: hydra_main declares
                // `WorkQueue workQueue` before `unique_ptr<GrpcServer>`, so
                // reverse destruction runs ~GrpcServer first and its
                // server_->Shutdown() blocks on in-flight RPCs -- which is
                // exactly this thread, now parked in future.get() instead of
                // in enqueue().  The hang moves rather than goes away.
                //
                lock.unlock();
                item->cancel();
                return future;
            }
            queue_.push(std::move(item));
        }
        return future;
    }

    // Process all pending work items. Called from the main event loop.
    void processPending(SessionManager& sessionMgr,
                        AccountManager& accounts,
                        const HydraConfig& config,
                        ProcessManager& procMgr) {
        std::queue<std::unique_ptr<WorkItem>> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            batch.swap(queue_);
        }
        // Space is free for waiters as soon as we take the batch.
        cv_space_.notify_all();
        while (!batch.empty()) {
            batch.front()->execute(sessionMgr, accounts, config, procMgr);
            batch.pop();
        }
    }

    // Release any producer blocked on a full queue.  Call once the main
    // loop has stopped draining and before the gRPC server is torn down;
    // it is idempotent (#1286).
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_space_.notify_all();
    }

    // Check if there are pending items (for diagnostics).
    size_t pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_space_;
    std::queue<std::unique_ptr<WorkItem>> queue_;
    bool stopped_ = false;
};

#endif // HYDRA_WORK_QUEUE_H
