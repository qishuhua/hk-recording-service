#include "media/mp4_muxer.hpp"
#include <sys/stat.h>
#include <cstring>
#include <climits>
namespace hk {
int Mp4Muxer::write_cb(void* opaque,uint8_t* data,int size){auto* self=static_cast<Mp4Muxer*>(opaque);if(size<0)return AVERROR(EINVAL);const int fd=self->file_->fd();off_t pos=lseek(fd,0,SEEK_CUR);if(pos<0)return AVERROR(errno);if(uint64_t(pos)>self->limit_||uint64_t(size)>self->limit_-uint64_t(pos))return AVERROR(ENOSPC);int n=0;while(n<size){ssize_t ret=write(fd,data+n,size-n);if(ret<0&&errno==EINTR)continue;if(ret<=0)return AVERROR(ret<0?errno:EIO);n+=int(ret);}return n;}
int64_t Mp4Muxer::seek_cb(void* opaque,int64_t off,int whence){auto* self=static_cast<Mp4Muxer*>(opaque);const int fd=self->file_->fd();if(whence&AVSEEK_SIZE){struct stat st{};return fstat(fd,&st)==0?st.st_size:AVERROR(errno);}whence&=~AVSEEK_FORCE;if(whence!=SEEK_SET&&whence!=SEEK_CUR&&whence!=SEEK_END)return AVERROR(EINVAL);off_t result=lseek(fd,off,whence);return result<0?AVERROR(errno):result;}
void Mp4Muxer::check(int ret,const char* stage){if(ret>=0)return;char buf[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(ret,buf,sizeof(buf));throw Error(ret==AVERROR(ENOSPC)?507:502,ret==AVERROR(ENOSPC)?"storage_limit":"remux_failed",std::string(stage)+": "+buf);}
void Mp4Muxer::cleanup()noexcept{if(pkt_)av_packet_free(&pkt_);if(fmt_){fmt_->pb=nullptr;avformat_free_context(fmt_);fmt_=nullptr;}if(io_){av_freep(&io_->buffer);avio_context_free(&io_);}}
void Mp4Muxer::init(const Frame& f){
 extra_=parameter_sets(f.data,codec_);if(extra_.empty())throw Error(502,"missing_parameter_sets","首个 I 帧缺少完整 SPS/PPS/VPS，不能安全封装");
 if(!f.width||!f.height)throw Error(502,"invalid_resolution","SDK 未提供有效分辨率");
 width_=f.width;height_=f.height;base_=f.pts;first_=f.osd;
 check(avformat_alloc_output_context2(&fmt_,nullptr,"mp4",nullptr),"alloc mp4");if(!fmt_)throw std::bad_alloc();stream_=avformat_new_stream(fmt_,nullptr);if(!stream_)throw std::bad_alloc();auto* p=stream_->codecpar;p->codec_type=AVMEDIA_TYPE_VIDEO;p->codec_id=codec_==Codec::H264?AV_CODEC_ID_H264:AV_CODEC_ID_HEVC;p->codec_tag=codec_==Codec::H265?MKTAG('h','v','c','1'):0;p->width=width_;p->height=height_;
 p->extradata=static_cast<uint8_t*>(av_mallocz(extra_.size()+AV_INPUT_BUFFER_PADDING_SIZE));if(!p->extradata)throw std::bad_alloc();p->extradata_size=int(extra_.size());memcpy(p->extradata,extra_.data(),extra_.size());stream_->time_base={1,1000};
 auto* buf=static_cast<uint8_t*>(av_malloc(32768));if(!buf)throw std::bad_alloc();io_=avio_alloc_context(buf,32768,1,this,nullptr,&write_cb,&seek_cb);if(!io_){av_free(buf);throw std::bad_alloc();}io_->seekable=AVIO_SEEKABLE_NORMAL;fmt_->pb=io_;fmt_->flags|=AVFMT_FLAG_CUSTOM_IO;
 pkt_=av_packet_alloc();if(!pkt_)throw std::bad_alloc();check(avformat_write_header(fmt_,nullptr),"write mp4 header");initialized_=true;
}
void Mp4Muxer::write_pending(int64_t duration){deadline(end_);if(pending_.data.size()>INT_MAX)throw Error(502,"invalid_frame","帧过大");check(av_new_packet(pkt_,int(pending_.data.size())),"alloc packet");memcpy(pkt_->data,pending_.data.data(),pending_.data.size());pkt_->stream_index=stream_->index;pkt_->pts=pkt_->dts=pending_pts_;pkt_->duration=duration;pkt_->pos=-1;if(pending_.type==1)pkt_->flags|=AV_PKT_FLAG_KEY;av_packet_rescale_ts(pkt_,AVRational{1,1000},stream_->time_base);int ret=av_interleaved_write_frame(fmt_,pkt_);av_packet_unref(pkt_);check(ret,"write packet");check(io_->error,"write mp4 file");++frames_;last_=pending_.osd;}
void Mp4Muxer::push(Frame f){deadline(end_);if(f.mode!=0)throw Error(501,"unsupported_packet_mode","仅支持完整 Annex-B ES 帧，不支持 FU-A 分片");if(f.type==2)throw Error(501,"unsupported_b_frame","SDK 未提供可靠 DTS，暂不支持 B 帧");if(!f.osd.valid())throw Error(502,"invalid_osd","SDK 帧日历时间无效");if(!initialized_){if(f.type!=1)return;init(f);pending_=std::move(f);return;}
 if((f.width&&int(f.width)!=width_)||(f.height&&int(f.height)!=height_))throw Error(501,"resolution_changed","录像中途分辨率改变");
 if(f.type==1){auto sets=parameter_sets(f.data,codec_);if(!sets.empty()&&sets!=extra_)throw Error(501,"parameter_sets_changed","录像中途编码参数集改变");}
 int64_t pts=relative_pts(base_,f.pts);last_interval_=interval(pending_pts_,pts);write_pending(last_interval_);pending_pts_=pts;pending_=std::move(f);
}
Result Mp4Muxer::finish(){if(!initialized_)throw Error(404,"no_recording","没有可独立解码的录像关键帧");deadline(end_);write_pending(last_interval_);check(av_write_trailer(fmt_),"write mp4 trailer");avio_flush(io_);check(io_->error,"flush mp4");struct stat st{};if(fstat(file_->fd(),&st)||st.st_size<=0)throw Error(507,"storage_failed","读取 MP4 大小失败");deadline(end_);Result r;r.bytes=st.st_size;r.frames=frames_;r.duration=pending_pts_+last_interval_;r.width=width_;r.height=height_;r.codec=codec_;r.first=first_;r.last=last_;finished_=true;return r;}
}
