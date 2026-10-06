#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <winsock2.h>
class VoiceAudio {
public:
 VoiceAudio(); ~VoiceAudio();
 bool start(SOCKET socket,sockaddr_in server,uint16_t room,const std::string& name);
 void stop(); void set_muted(bool v){muted_=v;} bool muted()const{return muted_;}
 void push_encoded(const unsigned char* data,int size);\n uint16_t room() const{return room_;}
private:
 static void capture_cb(void*,void* input,const void*,unsigned int frames);
 static void playback_cb(void*,void*,void* output,unsigned int frames);
 bool init_codec();
 SOCKET socket_{INVALID_SOCKET}; sockaddr_in server_{}; uint16_t room_{}; std::string name_;
 std::atomic<bool> running_{false},muted_{false}; std::atomic<uint32_t> sequence_{0};
 void* encoder_{nullptr}; void* decoder_{nullptr}; void* capture_{nullptr}; void* playback_{nullptr};
 std::mutex queue_mutex_; std::vector<int16_t> queue_;
};
