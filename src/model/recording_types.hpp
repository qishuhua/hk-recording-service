#pragma once
#include "common/core.hpp"
namespace hk {
struct Request{std::string host,username,password;int port=8000,channel=1,stream_type=0,timeout_ms=120000;NvrTime start,end;};
struct Result{uint64_t bytes=0,frames=0;int64_t duration=0,elapsed=0;int width=0,height=0;Codec codec=Codec::H264;NvrTime first,last;};
struct Frame{std::vector<uint8_t> data;uint64_t pts=0;unsigned type=0,mode=0,width=0,height=0;NvrTime osd;};
}
