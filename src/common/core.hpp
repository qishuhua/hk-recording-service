#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>
namespace hk
{
    struct Error : std::runtime_error
    {
        int status;
        std::string code;
        Error(int s, std::string c, std::string m) : std::runtime_error(std::move(m)), status(s), code(std::move(c)) {}
    };
    using Clock = std::chrono::steady_clock;
    inline int64_t ms() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }
    inline void deadline(int64_t end)
    {
        if (ms() >= end)
            throw Error(504, "download_timeout", "录像下载超时");
    }
    struct NvrTime
    {
        int y = 0, m = 0, d = 0, h = 0, min = 0, s = 0, milli = 0;
        bool valid() const
        {
            if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || h < 0 || h > 23 || min < 0 || min > 59 || s < 0 || s > 59 || milli < 0 || milli > 999)
                return false;
            const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return d <= days[m - 1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        }
        // Gregorian civil time, deliberately independent of host timezone/DST.
        int64_t value() const
        {
            if (!valid())
                throw Error(400, "invalid_time", "无效的设备日历时间");
            int yy = y - (m <= 2);
            const int era = yy / 400, yo = yy - era * 400;
            const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const int64_t days = era * 146097LL + yo * 365 + yo / 4 - yo / 100 + doy - 719468;
            return ((days * 24 + h) * 60 * 60 + min * 60 + s) * 1000 + milli;
        }
        std::string text() const
        {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d", y, m, d, h, min, s, milli);
            return buf;
        }
    };
    inline NvrTime parse_time(const std::string &x)
    {
        auto fail = []()
        { throw Error(400, "invalid_time", "时间格式必须为 YYYY-MM-DD HH:MM:SS（设备本地时间）"); };
        if (x.size() != 19 || x[4] != '-' || x[7] != '-' || (x[10] != ' ' && x[10] != 'T') || x[13] != ':' || x[16] != ':')
            fail();
        for (size_t i = 0; i < x.size(); ++i)
            if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && (x[i] < '0' || x[i] > '9'))
                fail();
        NvrTime t{std::stoi(x.substr(0, 4)), std::stoi(x.substr(5, 2)), std::stoi(x.substr(8, 2)), std::stoi(x.substr(11, 2)), std::stoi(x.substr(14, 2)), std::stoi(x.substr(17, 2)), 0};
        if (!t.valid())
            fail();
        return t;
    }
    enum class Codec
    {
        H264,
        H265
    };
    inline Codec sdk_codec(unsigned n)
    {
        if (n == 0 || n == 1)
            return Codec::H264;
        if (n == 10)
            return Codec::H265;
        throw Error(501, "unsupported_codec", "仅支持 H.264/H.265");
    }
    inline const char *codec_name(Codec c) { return c == Codec::H264 ? "h264" : "hevc"; }
    // Scan Annex-B NAL units; select parameter sets from a complete key access unit.
    inline std::vector<uint8_t> parameter_sets(const std::vector<uint8_t> &b, Codec c)
    {
        auto start = [&](size_t i) -> size_t
        {if(i+3<=b.size()&&b[i]==0&&b[i+1]==0&&b[i+2]==1)return 3;if(i+4<=b.size()&&b[i]==0&&b[i+1]==0&&b[i+2]==0&&b[i+3]==1)return 4;return 0; };
        std::vector<uint8_t> v, s, p;
        for (size_t i = 0; i < b.size();)
        {
            size_t n = start(i);
            if (!n)
            {
                ++i;
                continue;
            }
            size_t a = i + n, j = a;
            while (j < b.size() && !start(j))
                ++j;
            size_t e = j;
            while (e > a && b[e - 1] == 0)
                --e;
            if (e > a && (c == Codec::H264 || e - a >= 2))
            {
                unsigned type = c == Codec::H264 ? b[a] & 31 : (b[a] >> 1) & 63;
                std::vector<uint8_t> *dest = nullptr;
                if (c == Codec::H264)
                {
                    if (type == 7)
                        dest = &s;
                    if (type == 8)
                        dest = &p;
                }
                else
                {
                    if (type == 32)
                        dest = &v;
                    if (type == 33)
                        dest = &s;
                    if (type == 34)
                        dest = &p;
                }
                if (dest)
                {
                    dest->insert(dest->end(), {0, 0, 0, 1});
                    dest->insert(dest->end(), b.begin() + a, b.begin() + e);
                }
            }
            i = j;
        }
        if (s.empty() || p.empty() || (c == Codec::H265 && v.empty()))
            return {};
        v.insert(v.end(), s.begin(), s.end());
        v.insert(v.end(), p.begin(), p.end());
        return v;
    }
    inline int64_t relative_pts(uint64_t base, uint64_t now)
    {
        if (now < base || now - base > uint64_t(INT64_MAX))
            throw Error(502, "invalid_timestamps", "媒体时间戳回退");
        return static_cast<int64_t>(now - base);
    }
    inline int64_t interval(int64_t a, int64_t b)
    {
        if (b <= a)
            throw Error(502, "invalid_timestamps", "媒体时间戳重复或回退");
        return b - a;
    }
}
