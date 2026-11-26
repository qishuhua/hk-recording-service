#pragma once
#include "model/recording_types.hpp"
#include "common/temp_file.hpp"
#include <memory>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <libavutil/error.h>
}
namespace hk
{
    class Mp4Muxer
    {
        std::shared_ptr<TempFile> file_;
        uint64_t limit_;
        int64_t end_;
        AVFormatContext *fmt_ = nullptr;
        AVIOContext *io_ = nullptr;
        AVStream *stream_ = nullptr;
        AVPacket *pkt_ = nullptr;
        bool initialized_ = false, finished_ = false;
        Codec codec_;
        uint64_t base_ = 0, frames_ = 0;
        int width_ = 0, height_ = 0;
        int64_t last_interval_ = 40;
        Frame pending_;
        int64_t pending_pts_ = 0;
        std::vector<uint8_t> extra_;
        NvrTime first_, last_;
        static int write_cb(void *, uint8_t *, int);
        static int64_t seek_cb(void *, int64_t, int);
        void check(int, const char *);
        void init(const Frame &);
        void write_pending(int64_t duration);
        void cleanup() noexcept;

    public:
        Mp4Muxer(std::shared_ptr<TempFile> f, uint64_t limit, int64_t end, Codec c) : file_(std::move(f)), limit_(limit), end_(end), codec_(c) {}
        ~Mp4Muxer() { cleanup(); }
        Mp4Muxer(const Mp4Muxer &) = delete;
        void push(Frame f);
        Result finish();
    };
}
