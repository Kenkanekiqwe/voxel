#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <atomic>
#include "audio.hpp"
#include "screen_capture.hpp"
#include "../common/protocol.hpp"
#pragma comment(lib,"ws2_32.lib")
static SOCKET g_sock=INVALID_SOCKET;static sockaddr_in g_server{};static VoiceAudio g_audio;static std::atomic<bool> g_running=true;static std::thread g_rx;
static HWND g_status,g_host,g_name,g_room,g_mute,g_screen;
static void set_status(const std::wstring&s){if(g_status)SetWindowTextW(g_status,s.c_str());}
static void receiver(){char buf[2048];while(g_running){sockaddr_in from{};int fl=sizeof(from);int n=recvfrom(g_sock,buf,sizeof(buf),0,(sockaddr*)&from,&fl);if(n<(int)sizeof(voxel::Header))continue;auto*h=(voxel::Header*)buf;if(h->magic!=voxel::MAGIC||h->type!=voxel::AUDIO||h->size<32)continue;int payload=h->size-32;if(payload>0&&payload<1276)g_audio.push_encoded((unsigned char*)buf+sizeof(voxel::Header)+32,payload);}}
static std::string wide_utf8(HWND w){wchar_t x[256];GetWindowTextW(w,x,256);int n=WideCharToMultiByte(CP_UTF8,0,x,-1,nullptr,0,nullptr,nullptr);std::string s(n? n-1:0,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,x,-1,s.data(),n,nullptr,nullptr);return s;}
static void connect_room(){
 std::string host=wide_utf8(g_host),name=wide_utf8(g_name);int room=std::stoi(wide_utf8(g_room));if(room<1)room=1;
 if(g_sock!=INVALID_SOCKET)closesocket(g_sock);g_sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(g_sock==INVALID_SOCKET){set_status(L"Socket error");return;}
 g_server.sin_family=AF_INET;g_server.sin_port=htons(40000);if(inet_pton(AF_INET,host.c_str(),&g_server.sin_addr)!=1){set_status(L"Invalid server IP");return;}
 unsigned long nb=1;ioctlsocket(g_sock,FIONBIO,&nb);
 char p[sizeof(voxel::Header)+32]{};auto*h=(voxel::Header*)p;h->magic=voxel::MAGIC;h->type=voxel::JOIN;h->room=(uint16_t)room;h->size=32;voxel::write_string(p+sizeof(voxel::Header),32,name.c_str());
 sendto(g_sock,p,sizeof(p),0,(sockaddr*)&g_server,sizeof(g_server));
 if(!g_audio.start(g_sock,g_server,(uint16_t)room,name)){set_status(L"Audio init failed");return;}
 g_rx=std::thread(receiver);set_status(L"Connected");
}
static LRESULT CALLBACK wndproc(HWND w,UINT m,WPARAM wp,LPARAM lp){
 if(m==WM_COMMAND){if((HWND)lp==g_mute){bool v=!g_audio.muted();g_audio.set_muted(v);SetWindowTextW(g_mute,v?L"Unmute":L"Mute");}
  else if((HWND)lp==g_screen){ScreenCapture c;wchar_t t[MAX_PATH];GetTempPathW(MAX_PATH,t);std::wstring p=std::wstring(t)+L"voxel_screen.jpg";set_status(c.capture_to_file(p)?L"Native screen captured to %TEMP%\\voxel_screen.jpg":c.last_error());}
  else if(LOWORD(wp)==1001)connect_room();}
 if(m==WM_DESTROY){g_running=false;if(g_sock!=INVALID_SOCKET)closesocket(g_sock);if(g_rx.joinable())g_rx.join();g_audio.stop();WSACleanup();PostQuitMessage(0);}return DefWindowProcW(w,m,wp,lp);
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE, PWSTR,int){
 WSADATA d{};WSAStartup(MAKEWORD(2,2),&d);WNDCLASSW wc{};wc.hInstance=h;wc.lpfnWndProc=wndproc;wc.lpszClassName=L"VoxelVoice";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
 HWND win=CreateWindowExW(0,wc.lpszClassName,L"Voxel Voice",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,720,480,nullptr,nullptr,h,nullptr);
 CreateWindowW(L"STATIC",L"Server",WS_CHILD|WS_VISIBLE,28,28,70,24,win,nullptr,h,nullptr);g_host=CreateWindowW(L"EDIT",L"127.0.0.1",WS_CHILD|WS_VISIBLE|WS_BORDER,100,24,210,28,win,nullptr,h,nullptr);
 CreateWindowW(L"STATIC",L"Name",WS_CHILD|WS_VISIBLE,28,72,70,24,win,nullptr,h,nullptr);g_name=CreateWindowW(L"EDIT",L"Player",WS_CHILD|WS_VISIBLE|WS_BORDER,100,68,210,28,win,nullptr,h,nullptr);
 CreateWindowW(L"STATIC",L"Room",WS_CHILD|WS_VISIBLE,28,116,70,24,win,nullptr,h,nullptr);g_room=CreateWindowW(L"EDIT",L"1",WS_CHILD|WS_VISIBLE|WS_BORDER,100,112,80,28,win,nullptr,h,nullptr);
 CreateWindowW(L"BUTTON",L"Connect",WS_CHILD|WS_VISIBLE,330,24,110,32,win,(HMENU)1001,h,nullptr);g_mute=CreateWindowW(L"BUTTON",L"Mute",WS_CHILD|WS_VISIBLE,28,180,110,34,win,nullptr,h,nullptr);g_screen=CreateWindowW(L"BUTTON",L"Screen Capture",WS_CHILD|WS_VISIBLE,154,180,150,34,win,nullptr,h,nullptr);g_status=CreateWindowW(L"STATIC",L"Not connected",WS_CHILD|WS_VISIBLE,28,240,600,30,win,nullptr,h,nullptr);
 MSG msg;while(GetMessageW(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
}
