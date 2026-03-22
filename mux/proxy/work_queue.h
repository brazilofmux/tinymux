#ifndef HYDRA_WORK_QUEUE_H
#define HYDRA_WORK_QUEUE_H

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

private:
    Func fn_;
    std::promise<Result> promise_;
};

// Thread-safe work queue. gRPC threads enqueue, main loop processes.
class WorkQueue {
public:
    // Enqueue a work item. Returns a future for the result.
    // Called from gRPC threads.
    template<typename Result>
    std::future<Result> enqueue(
        std::function<Result(SessionManager&, AccountManager&,
                             const HydraConfig&, ProcessManager&)> fn) {
        auto item = std::make_unique<TypedWorkItem<Result>>(std::move(fn));
        auto future = item->getFuture();
        {
            std::lock_guard<std::mutex> lock(mutex_);
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
        while (!batch.empty()) {
            batch.front()->execute(sessionMgr, accounts, config, procMgr);
            batch.pop();
        }
    }

    // Check if there are pending items (for diagnostics).
    size_t pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::queue<std::unique_ptr<WorkItem>> queue_;
};

#endif // HYDRA_WORK_QUEUE_H
