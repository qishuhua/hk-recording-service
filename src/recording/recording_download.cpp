#include "recording/recording_download.hpp"
#include "sdk/es_queue.hpp"
#include "media/mp4_muxer.hpp"
namespace hk
{
    namespace
    {
        struct Playback
        {
            LONG handle = -1;
            std::shared_ptr<Queue> q;
            explicit Playback(std::shared_ptr<Queue> a) : q(std::move(a)) {}
            void stop()
            {
                q->close();
                if (handle >= 0)
                {
                    LONG h = handle;
                    handle = -1;
                    if (!NET_DVR_StopPlayBack(h))
                        throw sdk_error("playback_stop_failed", "停止回放失败");
                }
            }
            ~Playback()
            {
                try
                {
                    stop();
                }
                catch (const std::exception &e)
                {
                    spdlog::error("{}", e.what());
                }
            }
            bool control(DWORD op) { return NET_DVR_PlayBackControl_V40(handle, op, nullptr, 0, nullptr, nullptr); }
        };
    }
    Result download(const Request &r, const Config &c, std::shared_ptr<TempFile> file, int64_t end)
    {
        const auto started = ms();
        deadline(end);
        Session session(r);
        deadline(end);
        Codec codec = query_codec(session.id(), r);
        deadline(end);
        auto q = std::make_shared<Queue>(c.queue);
        CallbackRegistry registry(q);
        Playback pb(q);
        Mp4Muxer mux(std::move(file), c.max_bytes, end, codec);
        NET_DVR_VOD_PARA vod{};
        vod.dwSize = sizeof(vod);
        vod.struIDInfo.dwSize = sizeof(vod.struIDInfo);
        vod.struIDInfo.dwChannel = r.channel;
        set_sdk_time(vod.struBeginTime, r.start);
        set_sdk_time(vod.struEndTime, r.end);
        vod.byStreamType = static_cast<BYTE>(r.stream_type);
        vod.byDrawFrame = 0;
        vod.hWnd = 0;
        pb.handle = NET_DVR_PlayBackByTime_V40(session.id(), &vod);
        if (pb.handle < 0)
        {
            auto e = NET_DVR_GetLastError(); // 设备的无录像错误码在不同 SDK/设备上需要真机核实，避免误判所有错误为 404。
            throw Error(502, "playback_open_failed", "打开回放失败，SDK 错误码 " + std::to_string(e));
        }
        if (!NET_DVR_SetPlayBackESCallBack(pb.handle, &CallbackRegistry::callback, registry.opaque()))
            throw sdk_error("callback_failed", "注册 ES 回调失败");
        DWORD offset = 0;
        if (!NET_DVR_PlayBackControl_V40(pb.handle, NET_DVR_PLAYSTART, &offset, sizeof(offset), nullptr, nullptr))
            throw sdk_error("playback_start_failed", "启动回放失败");
        // 全速回放是否由设备支持决定，不强行使用倍速控制，避免抽帧。
        bool eof = false, paused = false;
        int64_t next_poll = ms(), next_ping = ms() + 2000;
        uint64_t input_bytes = 0;
        unsigned control_errors = 0;
        for (;;)
        {
            deadline(end);
            Frame f;
            if (q->pop(f))
            {
                if (f.data.size() > c.max_bytes - input_bytes)
                    throw Error(507, "storage_limit", "录像超出单文件容量限制");
                input_bytes += f.data.size();
                mux.push(std::move(f));
            }
            auto now = ms();
            auto state = q->state();
            if (!eof && !paused && state.bytes >= c.queue / 2)
            {
                if (pb.control(NET_DVR_PLAYPAUSE))
                    paused = true;
                else
                    spdlog::warn("pause failed sdk={}", NET_DVR_GetLastError());
            }
            else if (paused && state.bytes <= c.queue / 8)
            {
                if (!pb.control(NET_DVR_PLAYRESTART))
                    throw sdk_error("playback_resume_failed", "恢复回放失败");
                paused = false;
            }
            if (now >= next_poll)
            {
                DWORD pos = 0, len = sizeof(pos);
                if (NET_DVR_PlayBackControl_V40(pb.handle, NET_DVR_PLAYGETPOS, nullptr, 0, &pos, &len) && len == sizeof(pos))
                {
                    control_errors = 0;
                    if (pos == 200)
                        throw Error(502, "playback_error", "设备回放进度返回 200");
                    if (pos == 100)
                        eof = true;
                }
                else if (++control_errors >= 5)
                    throw sdk_error("playback_progress_failed", "连续查询回放进度失败");
                next_poll = now + 250;
            }
            if (now >= next_ping)
            {
                pb.control(NET_DVR_KEEPALIVE);
                next_ping = now + 2000;
            }
            if (eof && q->seal_if_idle(ms(), c.quiet))
                break;
            if (!eof && !paused && state.empty && now - state.last >= c.idle)
                throw Error(504, "playback_no_data", "回放数据长时间未到达");
        }
        pb.stop();
        deadline(end);
        auto result = mux.finish();
        result.elapsed = ms() - started;
        return result;
    }
}
