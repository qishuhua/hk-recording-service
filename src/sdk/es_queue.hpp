#pragma once
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include "sdk/sdk.hpp"
namespace hk
{
    // Callback registry uses non-dereferenced monotonically increasing tokens. Late callbacks cannot access freed objects.
    struct Queue
    {
        explicit Queue(uint64_t n) : cap(n), last(ms()) {}
        std::mutex mu;
        std::condition_variable cv;
        std::deque<Frame> frames;
        const uint64_t cap;
        uint64_t bytes = 0;
        int64_t last;
        bool closed = false, failed = false;
        const char *reason = "";
        void push(const NET_DVR_PACKET_INFO_EX &p)
        {
            std::lock_guard<std::mutex> l(mu);
            if (closed || failed)
                return;
            if (p.dwPacketType < 1 || p.dwPacketType > 3)
                return;
            const uint64_t cost = uint64_t(p.dwPacketSize) + 256;
            if (!p.pPacketBuffer || !p.dwPacketSize || cost > cap - bytes)
            {
                failed = true;
                reason = "ES 队列溢出或空帧";
                cv.notify_one();
                return;
            }
            Frame f;
            f.data.assign(p.pPacketBuffer, p.pPacketBuffer + p.dwPacketSize);
            f.pts = (uint64_t(p.dwTimeStampHigh) << 32) | p.dwTimeStamp;
            f.type = p.dwPacketType;
            f.mode = p.dwPacketMode;
            f.width = p.wWidth;
            f.height = p.wHeight;
            f.osd = {int(p.dwYear), int(p.dwMonth), int(p.dwDay), int(p.dwHour), int(p.dwMinute), int(p.dwSecond), int(p.dwMillisecond)};
            frames.push_back(std::move(f));
            bytes += cost;
            last = ms();
            cv.notify_one();
        }
        void fail() noexcept
        {
            try
            {
                std::lock_guard<std::mutex> l(mu);
                failed = true;
                reason = "ES 回调异常";
                cv.notify_one();
            }
            catch (...)
            {
            }
        }
        bool pop(Frame &f)
        {
            std::unique_lock<std::mutex> l(mu);
            cv.wait_for(l, std::chrono::milliseconds(50), [&]
                        { return failed || closed || !frames.empty(); });
            if (failed)
                throw Error(503, "es_queue_failed", reason);
            if (frames.empty())
                return false;
            f = std::move(frames.front());
            frames.pop_front();
            bytes -= f.data.size() + 256;
            return true;
        }
        struct State
        {
            uint64_t bytes;
            int64_t last;
            bool empty;
        };
        State state()
        {
            std::lock_guard<std::mutex> l(mu);
            if (failed)
                throw Error(503, "es_queue_failed", reason);
            return {bytes, last, frames.empty()};
        }
        // Atomic EOF seal: prevents a race with the next callback between idle check and closing.
        bool seal_if_idle(int64_t now, int quiet)
        {
            std::lock_guard<std::mutex> l(mu);
            if (failed)
                throw Error(503, "es_queue_failed", reason);
            if (frames.empty() && now - last >= quiet)
            {
                closed = true;
                return true;
            }
            return false;
        }
        void close()
        {
            std::lock_guard<std::mutex> l(mu);
            closed = true;
            cv.notify_all();
        }
    };
    class CallbackRegistry
    {
        inline static std::mutex mu_;
        inline static std::unordered_map<uintptr_t, std::shared_ptr<Queue>> entries_;
        inline static std::atomic<uintptr_t> next_{1};
        uintptr_t id_;

    public:
        explicit CallbackRegistry(std::shared_ptr<Queue> q) : id_(next_.fetch_add(1))
        {
            std::lock_guard<std::mutex> l(mu_);
            entries_.emplace(id_, std::move(q));
        }
        ~CallbackRegistry()
        {
            std::lock_guard<std::mutex> l(mu_);
            entries_.erase(id_);
        }
        CallbackRegistry(const CallbackRegistry &) = delete;
        void *opaque() const { return reinterpret_cast<void *>(id_); }
        static void CALLBACK callback(LONG, NET_DVR_PACKET_INFO_EX *p, void *token) noexcept
        {
            std::shared_ptr<Queue> q;
            try
            {
                {
                    std::lock_guard<std::mutex> l(mu_);
                    auto i = entries_.find(reinterpret_cast<uintptr_t>(token));
                    if (i != entries_.end())
                        q = i->second;
                }
                if (q && p)
                    q->push(*p);
            }
            catch (...)
            {
                if (q)
                    q->fail();
            }
        }
    };
}
