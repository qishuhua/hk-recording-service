#pragma once
#include "model/recording_types.hpp"
#include "config/config.hpp"
#include "common/temp_file.hpp"
namespace hk
{
    Result download(const Request &, const Config &, std::shared_ptr<TempFile>, int64_t deadline_ms);
}
