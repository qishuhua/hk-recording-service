#pragma once
#include <cstdlib>
#include <cerrno>
#include <string>
#include "common/core.hpp"
namespace hk
{
    inline std::string env(const char *k, const char *def)
    {
        const char *v = std::getenv(k);
        return v && *v ? v : def;
    }
    inline int number(const char *k, int def, int lo, int hi)
    {
        auto s = env(k, "");
        if (s.empty())
            return def;
        char *e = nullptr;
        errno = 0;
        long n = std::strtol(s.c_str(), &e, 10);
        if (errno || *e || n < lo || n > hi)
            throw std::runtime_error(std::string("invalid environment: ") + k);
        return int(n);
    }
    struct Config
    {
        std::string addr = env("HKMEDIA_LISTEN_ADDR", "0.0.0.0"), temp = env("HKMEDIA_TEMP_DIR", "/data/temp"), web = env("HKMEDIA_WEB_ROOT", "./web"), logdir = env("LOGDIR", "/data/logs"), level = env("HKMEDIA_LOG_LEVEL", "info");
        int port = number("HKMEDIA_PORT", 8181, 1, 65535), global = number("HKMEDIA_MAX_CONCURRENT", 16, 1, 64), timeout = number("HKMEDIA_DEFAULT_TIMEOUT_MS", 120000, 1000, 14400000), max_duration = number("HKMEDIA_MAX_DURATION_SECONDS", 7200, 1, 7200), connect = number("HKMEDIA_CONNECT_TIMEOUT_MS", 5000, 1000, 60000), idle = number("HKMEDIA_PLAYBACK_IDLE_MS", 15000, 1000, 120000), quiet = number("HKMEDIA_EOF_QUIET_MS", 2000, 500, 10000);
        uint64_t queue = uint64_t(number("HKMEDIA_QUEUE_MIB", 32, 4, 256)) * 1024 * 1024;
        uint64_t max_bytes = uint64_t(number("HKMEDIA_MAX_FILE_MIB", 2048, 1, 65536)) * 1024 * 1024;
        bool sdk_log = number("HKMEDIA_SDK_LOG", 0, 0, 1) != 0;
    };
}
