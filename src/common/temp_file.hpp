#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <memory>
#include "common/core.hpp"
namespace hk
{
    class TempFile
    {
        int fd_ = -1;

    public:
        explicit TempFile(const std::string &dir)
        {
            std::string path = dir + "/.hk-recording-XXXXXX";
            std::vector<char> p(path.begin(), path.end());
            p.push_back(0);
            fd_ = mkstemp(p.data());
            if (fd_ < 0)
                throw Error(507, "storage_failed", "创建临时文件失败");
            if (unlink(p.data()) != 0)
            {
                close(fd_);
                fd_ = -1;
                throw Error(507, "storage_failed", "删除临时文件目录项失败");
            }
            fcntl(fd_, F_SETFD, FD_CLOEXEC);
        }
        ~TempFile()
        {
            if (fd_ >= 0)
                close(fd_);
        }
        TempFile(const TempFile &) = delete;
        int fd() const { return fd_; }
    };
}
