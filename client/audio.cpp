#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio.hpp"
#include "../common/protocol.hpp"
#include <opus/opus.h>
#include <algorithm>
#include <cstring>
VoiceAudio::VoiceAudio(){}
VoiceAudio::~VoiceAudio(){stop();}
bool VoiceAudio::init_codec(){
 int err=OPUS_OK;
 encoder_=opus_encoder_create(48000,1,OPUS_APPLICATION_VOIP,&err); if(err!=OPUS_OK)return false;
 decoder_=opus_decoder_create(48000,1,&err); if(err!=OPUS_OK)return false;
 opus_encoder_ctl((OpusEncoder*)encoder_,OPUS_SET_BITRATE(32000));
 opus_encoder_ctl((OpusEncoder*)encoder_,OPUS_SET_COMPLEXITY(5));
 opus_encoder_ctl((OpusEncoder*)encoder_,OPUS_SET_INBAND_FEC(1));
 opus_encoder_ctl((OpusEncoder*)encoder_,OPUS_SET_PACKET_LOSS_PERC(5));
 return true;
}
bool VoiceAudio::start(SOCKET socket,sockaddr_in server,uint16_t room,const std::string& name){
 if(running_)return true; socket_=socket;server_=server;room_=room;name_=name;
 if(!init_codec())return false;
 auto cap=new ma_device; auto cc=ma_device_config_init(ma_device_type_capture);
 cc.capture.format=ma_format_s16;cc.capture.channels=1;cc.sampleRate=48000;cc.periodSizeInFrames=480;cc.dataCallback=capture_cb;cc.pUserData=this;
 if(ma_device_init(nullptr,&cc,cap)!=MA_SUCCESS){delete cap;return false;} capture_=cap;
 auto play=new ma_device; auto pc=ma_device_config_init(ma_device_type_playback);
 pc.playback.format=ma_format_s16;pc.playback.channels=1;pc.sampleRate=48000;pc.periodSizeInFrames=480;pc.dataCallback=playback_cb;pc.pUserData=this;
 if(ma_device_init(nullptr,&pc,play)!=MA_SUCCESS){ma_device_uninit(cap);delete cap;delete play;capture_=nullptr;return false;} playback_=play;
 running_=true; if(ma_device_start(cap)!=MA_SUCCESS||ma_device_start(play)!=MA_SUCCESS){stop();return false;} return true;
}
void VoiceAudio::stop(){
 if(capture_){auto d=(ma_device*)capture_;ma_device_uninit(d);delete d;capture_=nullptr;}
 if(playback_){auto d=(ma_device*)playback_;ma_device_uninit(d);delete d;playback_=nullptr;}
 running_=false;
 if(encoder_){opus_encoder_destroy((OpusEncoder*)encoder_);encoder_=nullptr;}
 if(decoder_){opus_decoder_destroy((OpusDecoder*)decoder_);decoder_=nullptr;}
 std::lock_guard lock(queue_mutex_);queue_.clear();
}
void VoiceAudio::push_encoded(const unsigned char* data,int size){
 if(!decoder_||size<=0)return; int16_t pcm[960];
 int n=opus_decode((OpusDecoder*)decoder_,data,size,pcm,960,0); if(n<=0)return;
 std::lock_guard lock(queue_mutex_);
 queue_.insert(queue_.end(),pcm,pcm+n);
 if(queue_.size()>48000) queue_.erase(queue_.begin(),queue_.begin()+(queue_.size()-48000));
}
void VoiceAudio::capture_cb(void* user,void* input,const void*,unsigned int frames){
 auto*self=(VoiceAudio*)user;if(!self->running_||self->muted_||!input)return;
 unsigned char enc[1275]; int n=opus_encode((OpusEncoder*)self->encoder_,(const int16_t*)input,(int)frames,enc,sizeof(enc));if(n<=0)return;
 unsigned char packet[sizeof(voxel::Header)+32+1275];auto*h=(voxel::Header*)packet;
 h->magic=voxel::MAGIC;h->type=voxel::AUDIO;h->room=self->room_;h->sequence=++self->sequence_;h->size=(uint16_t)(32+n);
 voxel::write_string((char*)(packet+sizeof(voxel::Header)),32,self->name_.c_str());
 std::memcpy(packet+sizeof(voxel::Header)+32,enc,n);
 sendto(self->socket_,(const char*)packet,(int)(sizeof(voxel::Header)+32+n),0,(sockaddr*)&self->server_,sizeof(self->server_));
}
void VoiceAudio::playback_cb(void* user,void*,void* output,unsigned int frames){
 auto*self=(VoiceAudio*)user;auto*dst=(int16_t*)output;std::fill(dst,dst+frames,0);
 std::lock_guard lock(self->queue_mutex_);size_t n=std::min<size_t>(frames,self->queue_.size());
 std::copy_n(self->queue_.begin(),n,dst);self->queue_.erase(self->queue_.begin(),self->queue_.begin()+n);
}
