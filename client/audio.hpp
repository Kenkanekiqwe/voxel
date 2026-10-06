#include "miniaudio.h"
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>
#include <winsock2.h>
class VoiceAudio {
public:
 VoiceAudio(); ~VoiceAudio();
 bool start(SOCKET socket,sockaddr_in server,uint16_t room,const std::string& name);
 void stop(); void set_muted(bool v){muted_=v;} bool muted()const{return muted_;}
 void push_encoded(const unsigned char* data,int size,const std::string& speaker);
 uint16_t room() const{return room_;}
private:
 static void capture_cb(ma_device* device, void* output, const void* input, ma_uint32 frameCount);
 static void playback_cb(ma_device* device, void* output, const void* input, ma_uint32 frameCount);
 bool init_codec();
 SOCKET socket_{INVALID_SOCKET}; sockaddr_in server_{}; uint16_t room_{}; std::string name_;
 std::atomic<bool> running_{false},muted_{false}; std::atomic<uint32_t> sequence_{0};
 void* encoder_{nullptr};
 std::unordered_map<std::string,void*> decoders_; void* capture_{nullptr}; void* playback_{nullptr};
 std::mutex queue_mutex_; std::vector<int16_t> queue_;
};
