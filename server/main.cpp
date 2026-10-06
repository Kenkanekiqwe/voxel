#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <vector>
#include <mutex>
#include <algorithm>
#include "../common/protocol.hpp"
#pragma comment(lib,"ws2_32.lib")
struct Client{sockaddr_in addr{};uint16_t room{};};
static std::vector<Client> clients;static std::mutex mx;
static bool same(const sockaddr_in&a,const sockaddr_in&b){return a.sin_addr.s_addr==b.sin_addr.s_addr&&a.sin_port==b.sin_port;}
static void upsert(const sockaddr_in&addr,uint16_t room){auto it=std::find_if(clients.begin(),clients.end(),[&](auto&c){return same(c.addr,addr);});if(it==clients.end())clients.push_back({addr,room});else it->room=room;}
static void relay(SOCKET s,const char*buf,int n,uint16_t room,const sockaddr_in&from){for(const auto&c:clients)if(c.room==room&&!same(c.addr,from))sendto(s,buf,n,0,(const sockaddr*)&c.addr,sizeof(c.addr));}
int main(){
 WSADATA w{};if(WSAStartup(MAKEWORD(2,2),&w)!=0)return 1;
 SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==INVALID_SOCKET)return 2;
 sockaddr_in b{};b.sin_family=AF_INET;b.sin_addr.s_addr=INADDR_ANY;b.sin_port=htons(40000);
 if(::bind(s,(sockaddr*)&b,sizeof(b))==SOCKET_ERROR)return 3;
 std::printf("Voxel Voice Server: UDP 40000\n");
 char buf[2048];
 for(;;){
  sockaddr_in from{};int fl=sizeof(from);int n=recvfrom(s,buf,sizeof(buf),0,(sockaddr*)&from,&fl);
  if(n<(int)sizeof(voxel::Header))continue;auto*h=(voxel::Header*)buf;
  if(h->magic!=voxel::MAGIC)continue;
  if(h->type==voxel::JOIN){
   if(h->size!=voxel::NAME_BYTES||n<(int)sizeof(voxel::Header)+voxel::NAME_BYTES)continue;
   std::lock_guard lock(mx);upsert(from,h->room);std::printf("join room %u\n",h->room);
  }else if(h->type==voxel::AUDIO){
   if(h->size<voxel::NAME_BYTES||h->size>voxel::NAME_BYTES+1275)continue;
   if(n<(int)sizeof(voxel::Header)+h->size)continue;std::lock_guard lock(mx);relay(s,buf,n,h->room,from);
  }else if(h->type==voxel::SCREEN){
   if(h->size<sizeof(voxel::ScreenFragment)||h->size>sizeof(voxel::ScreenFragment)+1000)continue;
   if(n<(int)sizeof(voxel::Header)+h->size)continue;
   auto*f=(voxel::ScreenFragment*)(buf+sizeof(voxel::Header));if(f->count==0||f->index>=f->count||f->payload_size>1000)continue;
   std::lock_guard lock(mx);relay(s,buf,n,h->room,from);
  }else if(h->type==voxel::LEAVE){
   std::lock_guard lock(mx);clients.erase(std::remove_if(clients.begin(),clients.end(),[&](auto&c){return same(c.addr,from);}),clients.end());
  }
 }
}