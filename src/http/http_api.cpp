#include "http/http_api.hpp"
#include "recording/recording_download.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <csignal>
#include <pthread.h>
#include <thread>
#include <filesystem>
#include <array>
#include <cctype>
namespace hk {
namespace {
using Json=nlohmann::json;
struct Gate:std::enable_shared_from_this<Gate>{
 std::mutex m;int active=0;const int max_all,max_one;std::unordered_map<std::string,int> devices;
 Gate(int a,int b):max_all(a),max_one(b){}
 struct Lease{std::shared_ptr<Gate> g;std::string key;Lease(std::shared_ptr<Gate> a,std::string b):g(std::move(a)),key(std::move(b)){}~Lease(){std::lock_guard<std::mutex> l(g->m);--g->active;auto i=g->devices.find(key);if(--i->second==0)g->devices.erase(i);}};
 std::shared_ptr<Lease> acquire(const std::string& key){std::lock_guard<std::mutex> l(m);auto i=devices.find(key);if(active>=max_all)throw Error(429,"too_many_requests","全局下载并发已满");if(i!=devices.end()&&i->second>=max_one)throw Error(429,"device_busy","该设备下载并发已满");auto& count=devices[key];try{auto lease=std::make_shared<Lease>(shared_from_this(),key);++count;++active;return lease;}catch(...){if(count==0)devices.erase(key);throw;}}
};
std::string string_field(const Json& j,const char* k,size_t max,bool allow_empty=false){if(!j.contains(k)||!j[k].is_string())throw Error(400,"invalid_param",std::string(k)+" 必须为字符串");auto s=j[k].get<std::string>();if((!allow_empty&&s.empty())||s.size()>max||s.find('\0')!=std::string::npos)throw Error(400,"invalid_param",std::string(k)+" 长度非法");return s;}
int int_field(const Json& j,const char* k,int def,int low,int high){if(!j.contains(k))return def;const auto& v=j[k];if(!v.is_number_integer()||(v.is_number_unsigned()&&v.get<uint64_t>()>uint64_t(high)))throw Error(400,"invalid_param",std::string(k)+" 必须为合法整数");auto n=v.get<int64_t>();if(n<low||n>high)throw Error(400,"invalid_param",std::string(k)+" 超出范围");return int(n);}
Request parse(const std::string& body,const Config& c){auto j=Json::parse(body,nullptr,false);if(j.is_discarded()||!j.is_object())throw Error(400,"invalid_json","请求必须为 JSON 对象");Request r;r.host=string_field(j,"host",128);for(unsigned char ch:r.host)if(ch<=32||ch>=127||ch=='/'||ch=='\\'||ch=='@')throw Error(400,"invalid_param","host 仅支持设备 IP 或域名");r.username=string_field(j,"username",63);r.password=string_field(j,"password",63,true);r.port=int_field(j,"port",8000,1,65535);r.channel=int_field(j,"channel",1,1,65535);r.stream_type=int_field(j,"stream_type",0,0,2);r.timeout_ms=int_field(j,"timeout_ms",c.timeout,1000,14400000);r.start=parse_time(string_field(j,"start_time",19));r.end=parse_time(string_field(j,"end_time",19));auto delta=r.end.value()-r.start.value();if(delta<=0||delta>int64_t(c.max_duration)*1000)throw Error(400,"invalid_range","录像结束时间须晚于开始时间且不超过配置上限");return r;}
void reply_error(httplib::Response& res,int status,const std::string& code,const std::string& msg){res.status=status;res.set_content(Json{{"error",{{"code",code},{"message",msg}}}}.dump(-1,' ',false,Json::error_handler_t::replace),"application/json; charset=utf-8");if(status==429)res.set_header("Retry-After","1");}
std::string device_key(const Request& r){std::string s=r.host;for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s+":"+std::to_string(r.port);}
}
int run_server(const Config& cfg){
 httplib::Server svr;auto gate=std::make_shared<Gate>(cfg.global,cfg.per_device);std::atomic<uint64_t> ids{0};
 svr.set_payload_max_length(64*1024);svr.set_read_timeout(15,0);svr.set_write_timeout(30,0);svr.set_keep_alive_max_count(10);
 // Worker pool separate from recording admission. ThreadPool queue cap supported by Ubuntu 24.04 httplib.
 svr.new_task_queue=[&]{return new httplib::ThreadPool(static_cast<size_t>(cfg.global*2+4),64);};
 svr.Get("/healthz",[](const httplib::Request&,httplib::Response& res){res.set_content("ok","text/plain");});
 if(!svr.set_mount_point("/",cfg.web))spdlog::warn("web root unavailable: {}",cfg.web);
 svr.Post("/recordings",[&](const httplib::Request& req,httplib::Response& res){const auto start=ms();const std::string id=std::to_string(start)+"-"+std::to_string(++ids);res.set_header("X-Request-Id",id);res.set_header("Cache-Control","no-store");try{
   if(!cfg.token.empty()&&req.get_header_value("Authorization")!="Bearer "+cfg.token)throw Error(401,"unauthorized","服务鉴权失败");
   const auto ct=req.get_header_value("Content-Type");if(ct.substr(0,16)!="application/json")throw Error(415,"unsupported_content_type","请使用 application/json");
   auto r=parse(req.body,cfg);auto lease=gate->acquire(device_key(r));auto file=std::make_shared<TempFile>(cfg.temp);spdlog::info("request={} download start device={} channel={} range={} ~ {} stream={}",id,device_key(r),r.channel,r.start.text(),r.end.text(),r.stream_type);
   auto result=download(r,cfg,file,start+r.timeout_ms);res.status=200;res.set_header("Content-Disposition","attachment; filename=\"recording_ch"+std::to_string(r.channel)+".mp4\"");res.set_header("X-Recording-Start-Time",result.first.text());res.set_header("X-Recording-End-Time",result.last.text());res.set_header("X-Recording-Duration-Ms",std::to_string(result.duration));res.set_header("X-Recording-Frames",std::to_string(result.frames));res.set_header("X-Recording-Resolution",std::to_string(result.width)+"x"+std::to_string(result.height));res.set_header("X-Recording-Codec",codec_name(result.codec));res.set_header("X-Recording-Elapsed-Ms",std::to_string(ms()-start));
   // pread avoids shared seek state; both file and admission lease live until response destruction.
   res.set_content_provider(static_cast<size_t>(result.bytes),"video/mp4",[file,lease](size_t offset,size_t length,httplib::DataSink& sink){std::array<char,65536> buf{};while(length){size_t want=std::min(length,buf.size());ssize_t n=pread(file->fd(),buf.data(),want,static_cast<off_t>(offset));if(n<0&&errno==EINTR)continue;if(n<=0||!sink.write(buf.data(),size_t(n)))return false;offset+=size_t(n);length-=size_t(n);}return true;},[file,lease,id](bool ok){spdlog::info("request={} file transfer success={}",id,ok);});
   spdlog::info("request={} ready bytes={} frames={} duration_ms={} elapsed_ms={}",id,result.bytes,result.frames,result.duration,ms()-start);
 }catch(const Error& e){spdlog::warn("request={} code={} status={} elapsed_ms={}",id,e.code,e.status,ms()-start);reply_error(res,e.status,e.code,e.what());}catch(const std::exception&){spdlog::error("request={} internal error",id);reply_error(res,500,"internal_error","服务内部错误");}});
 svr.set_error_handler([](const httplib::Request&,httplib::Response& res){if(res.body.empty()&&res.status>=400)reply_error(res,res.status,"http_error","HTTP 请求失败");});
 // SIGTERM/SIGINT are blocked in main before SDK threads are created; sigwait is safe outside signal handlers.
 if(!svr.bind_to_port(cfg.addr,cfg.port)){spdlog::error("bind failed {}:{}",cfg.addr,cfg.port);return 1;}
 sigset_t signals;sigemptyset(&signals);sigaddset(&signals,SIGINT);sigaddset(&signals,SIGTERM);sigaddset(&signals,SIGUSR1);
 std::thread waiter([&]{int sig=0;if(sigwait(&signals,&sig)==0&&sig!=SIGUSR1)svr.stop();});
 spdlog::info("listening {}:{}",cfg.addr,cfg.port);bool ok=false;try{ok=svr.listen_after_bind();}catch(...){pthread_kill(waiter.native_handle(),SIGUSR1);waiter.join();throw;}
 pthread_kill(waiter.native_handle(),SIGUSR1);waiter.join();return ok?0:1;
}
}
