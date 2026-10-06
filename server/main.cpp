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
int main(){WSADATA w{};if(WSAStartup(MAKEWORD(2,2),&w)!=0)return 1;SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==INVALID_SOCKET)return 2;sockaddr_in b{};b.sin_family=AF_INET;b.sin_addr.s_addr=INADDR_ANY;b.sin_port=htons(40000);if(::bind(s,(sockaddr*)&b,sizeof(b))==SOCKET_ERROR)return 3;std::printf("Voxel Voice Server: UDP 40000\n");
 char buf[2048];for(;;){sockaddr_in from{};int fl=sizeof(from);int n=recvfrom(s,buf,sizeof(buf),0,(sockaddr*)&from,&fl);if(n<(int)sizeof(voxel::Header))continue;auto*h=(voxel::Header*)buf;if(h->magic!=voxel::MAGIC)continue;
  if(h->type==voxel::JOIN){std::lock_guard lock(mx);auto it=std::find_if(clients.begin(),clients.end(),[&](auto&c){return same(c.addr,from);});if(it==clients.end())clients.push_back({from,h->room});else it->room=h->room;std::printf("join room %u\n",h->room);}
  else if(h->type==voxel::AUDIO){std::lock_guard lock(mx);for(auto&c:clients)if(c.room==h->room&&!same(c.addr,from))sendto(s,buf,n,0,(sockaddr*)&c.addr,sizeof(c.addr));}
  else if(h->type==voxel::LEAVE){std::lock_guard lock(mx);clients.erase(std::remove_if(clients.begin(),clients.end(),[&](auto&c){return same(c.addr,from);}),clients.end());}
 } }
