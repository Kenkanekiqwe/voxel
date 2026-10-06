#pragma once
#include <cstdint>
#include <cstring>
namespace voxel {
constexpr uint32_t MAGIC=0x314C5856;
constexpr uint16_t AUDIO=1,JOIN=2,LEAVE=3,PING=4;
#pragma pack(push,1)
struct Header { uint32_t magic; uint16_t type; uint16_t room; uint32_t sequence; uint16_t size; };
#pragma pack(pop)
inline void write_string(char* dst,size_t cap,const char* src){if(!cap)return;std::strncpy(dst,src,cap-1);dst[cap-1]=0;}
}
