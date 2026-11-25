#pragma once
#include <HCNetSDK.h>
#include <cstring>
#include <spdlog/spdlog.h>
#include "config/config.hpp"
#include "model/recording_types.hpp"
namespace hk {
inline Error sdk_error(const std::string& code,const std::string& what,int status=502){return Error(status,code,what+"，SDK 错误码 "+std::to_string(NET_DVR_GetLastError()));}
class Runtime{
public:
 explicit Runtime(const Config& c){if(!NET_DVR_Init())throw sdk_error("sdk_init_failed","SDK 初始化失败");NET_DVR_SetConnectTime(c.connect,1);NET_DVR_SetRecvTimeOut(c.connect);if(c.sdk_log)NET_DVR_SetLogToFile(3,const_cast<char*>("sdk_log"),TRUE);}
 ~Runtime(){NET_DVR_Cleanup();} Runtime(const Runtime&)=delete;
};
template<size_t N> void fixed(char(&dst)[N],const std::string& s){if(s.size()>=N||s.find('\0')!=std::string::npos)throw Error(400,"invalid_param","设备连接字段超过 SDK 限制或包含 NUL");std::memcpy(dst,s.c_str(),s.size()+1);}
class Session{
 LONG id_=-1;
public:
 explicit Session(const Request& r){NET_DVR_USER_LOGIN_INFO in{};NET_DVR_DEVICEINFO_V40 out{};fixed(in.sDeviceAddress,r.host);fixed(in.sUserName,r.username);fixed(in.sPassword,r.password);in.wPort=static_cast<WORD>(r.port);in.bUseAsynLogin=FALSE;id_=NET_DVR_Login_V40(&in,&out);if(id_<0){auto e=NET_DVR_GetLastError();throw Error(e==NET_DVR_PASSWORD_ERROR||e==NET_DVR_NOENOUGHPRI?401:502,"login_failed","设备登录失败，SDK 错误码 "+std::to_string(e));}}
 ~Session(){if(id_>=0&&!NET_DVR_Logout(id_))spdlog::warn("SDK logout failed: {}",NET_DVR_GetLastError());}Session(const Session&)=delete;LONG id()const{return id_;}
};
inline Codec query_codec(LONG id,const Request& r){
 NET_DVR_MULTI_STREAM_COMPRESSIONCFG_COND cond{};cond.dwSize=sizeof(cond);cond.struStreamInfo.dwSize=sizeof(cond.struStreamInfo);cond.struStreamInfo.dwChannel=r.channel;cond.dwStreamType=r.stream_type==2?3:r.stream_type;
 NET_DVR_MULTI_STREAM_COMPRESSIONCFG cfg{};cfg.dwSize=sizeof(cfg);DWORD status=0xffffffff;
 if(NET_DVR_GetDeviceConfig(id,NET_DVR_GET_MULTI_STREAM_COMPRESSIONCFG,1,&cond,sizeof(cond),&status,&cfg,sizeof(cfg))&&status<=1)return sdk_codec(cfg.struStreamPara.byVideoEncType);
 if(r.stream_type==2)throw sdk_error("codec_query_failed","三码流编码查询失败（不回退主码流）");
 NET_DVR_COMPRESSIONCFG_V30 old{};old.dwSize=sizeof(old);DWORD len=0;
 if(!NET_DVR_GetDVRConfig(id,NET_DVR_GET_COMPRESSCFG_V30,r.channel,&old,sizeof(old),&len))throw sdk_error("codec_query_failed","通道编码查询失败");
 return sdk_codec((r.stream_type==1?old.struNetPara:old.struNormHighRecordPara).byVideoEncType);
}
template<class T> inline void set_sdk_time(T& x,const NvrTime& t){x.dwYear=t.y;x.dwMonth=t.m;x.dwDay=t.d;x.dwHour=t.h;x.dwMinute=t.min;x.dwSecond=t.s;}
}
