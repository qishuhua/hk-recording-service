#include "common/core.hpp"
#include <iostream>
using namespace hk;
#define CHECK(x) do { if(!(x)) throw std::runtime_error("test failed: " #x); } while(0)
template<class F> void reject(F f){bool rejected=false;try{f();}catch(const Error&){rejected=true;}CHECK(rejected);}
int main(){
 CHECK(parse_time("1970-01-01 00:00:00").value()==0);
 CHECK(parse_time("2024-03-01T00:00:00").value()-parse_time("2024-02-28 00:00:00").value()==172800000);
 reject([]{parse_time("2026-02-29 00:00:00");});reject([]{parse_time("2024-04-31 00:00:00");});reject([]{parse_time("2024-01-01 00:00:00Z");});reject([]{parse_time("2024-01-01 24:00:00");});
 CHECK(relative_pts(1000,1040)==40);CHECK(interval(0,40)==40);reject([]{relative_pts(1000,999);});reject([]{interval(40,40);});
 CHECK(sdk_codec(0)==Codec::H264);CHECK(sdk_codec(10)==Codec::H265);reject([]{sdk_codec(99);});
 std::vector<uint8_t> h264={0,0,0,1,0x67,0xaa,0,0,1,0x68,0xbb,0,0,1,0x65,0xcc};CHECK(parameter_sets(h264,Codec::H264)==std::vector<uint8_t>({0,0,0,1,0x67,0xaa,0,0,0,1,0x68,0xbb}));
 std::vector<uint8_t> h265={0,0,1,0x40,1,2,0,0,1,0x42,1,3,0,0,0,1,0x44,1,4,0,0,1,0x26,1,5};CHECK(parameter_sets(h265,Codec::H265).size()==21);CHECK(parameter_sets(h264,Codec::H265).empty());CHECK(parameter_sets({0,0,1,0x65,1},Codec::H264).empty());
 std::cout<<"core tests passed\n";
}
