#include <windows.h>
#include <wincodec.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include "audio.hpp"
#include "screen_capture.hpp"
#include "../common/protocol.hpp"
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")

static SOCKET g_sock=INVALID_SOCKET;
static sockaddr_in g_server{};
static VoiceAudio g_audio;
static std::atomic<bool> g_running=false;
static std::atomic<bool> g_sharing=false;
static std::thread g_rx,g_screen_thread;
static HWND g_status,g_host,g_name,g_room,g_connect,g_mute,g_screen,g_preview;
static std::mutex g_frame_mutex;
static std::vector<unsigned char> g_remote_jpeg;
static uint32_t g_rx_frame_id=0;
static uint16_t g_rx_frame_count=0,g_rx_frame_received=0,g_rx_last_payload=0;
static std::vector<unsigned char> g_rx_frame_buffer;
static std::atomic<uint32_t> g_tx_frame_id=0;

static void set_status(const std::wstring&s){if(g_status)SetWindowTextW(g_status,s.c_str());}

static bool decode_and_draw_jpeg(HDC dc,const RECT& rc,const std::vector<unsigned char>& jpeg){
 if(jpeg.empty())return false;
 HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED); bool uninit=SUCCEEDED(init);
 IWICImagingFactory* factory=nullptr;IStream* stream=nullptr;IWICBitmapDecoder* decoder=nullptr;
 IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* conv=nullptr;bool ok=false;
 HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,jpeg.size());if(!mem)return false;
 void* dst=GlobalLock(mem);if(!dst){GlobalFree(mem);return false;}memcpy(dst,jpeg.data(),jpeg.size());GlobalUnlock(mem);
 HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
 if(SUCCEEDED(hr))hr=CreateStreamOnHGlobal(mem,TRUE,&stream);
 if(SUCCEEDED(hr))hr=factory->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder);
 if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);
 if(SUCCEEDED(hr))hr=factory->CreateFormatConverter(&conv);
 if(SUCCEEDED(hr))hr=conv->Initialize(frame,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom);
 if(SUCCEEDED(hr)){
   UINT w=0,h=0;conv->GetSize(&w,&h);std::vector<unsigned char> pixels((size_t)w*h*4);WICRect wr{0,0,(INT)w,(INT)h};
   if(SUCCEEDED(conv->CopyPixels(&wr,w*4,(UINT)pixels.size(),pixels.data()))){
     BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=(LONG)w;bi.bmiHeader.biHeight=-(LONG)h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
     SetStretchBltMode(dc,HALFTONE);StretchDIBits(dc,rc.left,rc.top,rc.right-rc.left,rc.bottom-rc.top,0,0,(int)w,(int)h,pixels.data(),&bi,DIB_RGB_COLORS,SRCCOPY);ok=true;
   }
 }
 if(conv)conv->Release();if(frame)frame->Release();if(decoder)decoder->Release();if(stream)stream->Release();if(factory)factory->Release();
 if(uninit)CoUninitialize();return ok;
}

static void handle_screen_packet(const char* buf,int n){
 if(n<(int)(sizeof(voxel::Header)+sizeof(voxel::ScreenFragment)))return;
 auto*h=(const voxel::Header*)buf;if(h->type!=voxel::SCREEN||h->size<sizeof(voxel::ScreenFragment))return;
 auto*f=(const voxel::ScreenFragment*)(buf+sizeof(voxel::Header));int payload=h->size-(int)sizeof(voxel::ScreenFragment);
 if(f->count==0||f->index>=f->count||payload!=(int)f->payload_size||payload>1000)return;
 std::lock_guard lock(g_frame_mutex);
 if(f->frame_id!=g_rx_frame_id||f->count!=g_rx_frame_count){
   g_rx_frame_id=f->frame_id;g_rx_frame_count=f->count;g_rx_frame_received=0;g_rx_last_payload=0;g_rx_frame_buffer.assign((size_t)f->count*1000,0);
 }
 size_t offset=(size_t)f->index*1000;
 memcpy(g_rx_frame_buffer.data()+offset,buf+sizeof(voxel::Header)+sizeof(voxel::ScreenFragment),payload);
 ++g_rx_frame_received;
 if(g_rx_frame_received>=g_rx_frame_count){
   size_t total=(size_t)(f->count-1)*1000+g_rx_last_payload;
   g_remote_jpeg.assign(g_rx_frame_buffer.begin(),g_rx_frame_buffer.begin()+total);g_rx_frame_received=0;
   if(g_preview)InvalidateRect(g_preview,nullptr,FALSE);
 }
}

static void receiver(){
 char buf[1400];
 while(g_running){
   fd_set set;FD_ZERO(&set);FD_SET(g_sock,&set);timeval tv{0,100000};if(select(0,&set,nullptr,nullptr,&tv)<=0)continue;
   sockaddr_in from{};int fl=sizeof(from);int n=recvfrom(g_sock,buf,sizeof(buf),0,(sockaddr*)&from,&fl);if(n<(int)sizeof(voxel::Header))continue;
   auto*h=(voxel::Header*)buf;if(h->magic!=voxel::MAGIC)continue;
   if(h->type==voxel::AUDIO&&h->size>=voxel::NAME_BYTES){
     int payload=h->size-(int)voxel::NAME_BYTES;
     if(payload>0&&payload<=1275&&n>=(int)sizeof(voxel::Header)+voxel::NAME_BYTES+payload){
       std::string speaker((char*)buf+sizeof(voxel::Header), strnlen((char*)buf+sizeof(voxel::Header), voxel::NAME_BYTES));
       if(!speaker.empty())g_audio.push_encoded((unsigned char*)buf+sizeof(voxel::Header)+voxel::NAME_BYTES,payload,speaker);
     }
   }else if(h->type==voxel::SCREEN)handle_screen_packet(buf,n);
 }
}

static void screen_sender(){
 ScreenCapture capture;
 while(g_running&&g_sharing){
   std::vector<unsigned char> jpeg;
   if(capture.capture_jpeg(jpeg,960,540,55)&&!jpeg.empty()){
     constexpr size_t chunk=1000;uint16_t count=(uint16_t)((jpeg.size()+chunk-1)/chunk);
     if(count<=600){
       uint32_t id=++g_tx_frame_id;
       for(uint16_t i=0;i<count&&g_running&&g_sharing;i++){
         size_t off=(size_t)i*chunk;uint16_t bytes=(uint16_t)std::min(chunk,jpeg.size()-off);
         char packet[sizeof(voxel::Header)+sizeof(voxel::ScreenFragment)+chunk]{};
         auto*h=(voxel::Header*)packet;h->magic=voxel::MAGIC;h->type=voxel::SCREEN;h->room=g_audio.room();h->sequence=++g_tx_frame_id;h->size=(uint16_t)(sizeof(voxel::ScreenFragment)+bytes);
         auto*f=(voxel::ScreenFragment*)(packet+sizeof(voxel::Header));f->frame_id=id;f->index=i;f->count=count;f->payload_size=bytes;f->width=960;f->height=540;
         memcpy(packet+sizeof(voxel::Header)+sizeof(voxel::ScreenFragment),jpeg.data()+off,bytes);
         sendto(g_sock,packet,(int)(sizeof(voxel::Header)+sizeof(voxel::ScreenFragment)+bytes),0,(sockaddr*)&g_server,sizeof(g_server));
       }
     }
   }
   std::this_thread::sleep_for(std::chrono::milliseconds(125));
 }
}

static std::string wide_utf8(HWND w){
 wchar_t x[256];GetWindowTextW(w,x,256);int n=WideCharToMultiByte(CP_UTF8,0,x,-1,nullptr,0,nullptr,nullptr);std::string s(n?n-1:0,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,x,-1,s.data(),n,nullptr,nullptr);return s;
}

static void stop_connection(){
 g_sharing=false;g_running=false;
 if(g_sock!=INVALID_SOCKET){closesocket(g_sock);g_sock=INVALID_SOCKET;}
 if(g_rx.joinable())g_rx.join();if(g_screen_thread.joinable())g_screen_thread.join();g_audio.stop();
 SetWindowTextW(g_connect,L"Connect");SetWindowTextW(g_screen,L"Share Screen");
}

static void connect_room(){
 if(g_running)return;
 std::string host=wide_utf8(g_host),name=wide_utf8(g_name);int room=1;try{room=std::stoi(wide_utf8(g_room));}catch(...){set_status(L"Invalid room");return;}
 if(room<1||room>65535){set_status(L"Invalid room");return;}
 SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==INVALID_SOCKET){set_status(L"Socket error");return;}
 g_server={};g_server.sin_family=AF_INET;g_server.sin_port=htons(40000);
 if(inet_pton(AF_INET,host.c_str(),&g_server.sin_addr)!=1){closesocket(s);set_status(L"Invalid server IP");return;}
 unsigned long nb=1;ioctlsocket(s,FIONBIO,&nb);g_sock=s;
 char p[sizeof(voxel::Header)+voxel::NAME_BYTES]{};auto*h=(voxel::Header*)p;h->magic=voxel::MAGIC;h->type=voxel::JOIN;h->room=(uint16_t)room;h->size=voxel::NAME_BYTES;voxel::write_string(p+sizeof(voxel::Header),voxel::NAME_BYTES,name.c_str());
 sendto(g_sock,p,sizeof(p),0,(sockaddr*)&g_server,sizeof(g_server));
 if(!g_audio.start(g_sock,g_server,(uint16_t)room,name)){closesocket(g_sock);g_sock=INVALID_SOCKET;set_status(L"Audio init failed");return;}
 g_running=true;g_rx=std::thread(receiver);SetWindowTextW(g_connect,L"Connected");set_status(L"Connected — voice ready");
}

static LRESULT CALLBACK preview_proc(HWND w,UINT m,WPARAM,LPARAM){
 if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT r;GetClientRect(w,&r);FillRect(dc,&r,(HBRUSH)(COLOR_WINDOW+1));std::vector<unsigned char> jpeg;{std::lock_guard lock(g_frame_mutex);jpeg=g_remote_jpeg;}
   if(!decode_and_draw_jpeg(dc,r,jpeg)){SetBkMode(dc,TRANSPARENT);DrawTextW(dc,L"No incoming screen share",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}EndPaint(w,&ps);return 0;}
 return DefWindowProcW(w,m,0,0);
}

static LRESULT CALLBACK wndproc(HWND w,UINT m,WPARAM wp,LPARAM lp){
 if(m==WM_COMMAND){
   if((HWND)lp==g_mute){bool v=!g_audio.muted();g_audio.set_muted(v);SetWindowTextW(g_mute,v?L"Unmute":L"Mute");}
   else if((HWND)lp==g_screen&&g_running){
     bool v=!g_sharing;g_sharing=v;SetWindowTextW(g_screen,v?L"Stop Share":L"Share Screen");
     if(v){if(g_screen_thread.joinable())g_screen_thread.join();g_screen_thread=std::thread(screen_sender);set_status(L"Screen sharing enabled");}
     else set_status(L"Screen sharing stopped");
   }else if(LOWORD(wp)==1001)connect_room();
 }
 if(m==WM_CLOSE){stop_connection();DestroyWindow(w);return 0;}
 if(m==WM_DESTROY){WSACleanup();PostQuitMessage(0);return 0;}
 return DefWindowProcW(w,m,wp,lp);
}

int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int){
 WSADATA d{};if(WSAStartup(MAKEWORD(2,2),&d)!=0)return 1;
 WNDCLASSW wc{};wc.hInstance=h;wc.lpfnWndProc=wndproc;wc.lpszClassName=L"VoxelVoice";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
 WNDCLASSW pc{};pc.hInstance=h;pc.lpfnWndProc=preview_proc;pc.lpszClassName=L"VoxelPreview";pc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&pc);
 HWND win=CreateWindowExW(0,wc.lpszClassName,L"Voxel Voice",WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,980,650,nullptr,nullptr,h,nullptr);
 CreateWindowW(L"STATIC",L"Server",WS_CHILD|WS_VISIBLE,24,22,60,24,win,nullptr,h,nullptr);g_host=CreateWindowW(L"EDIT",L"127.0.0.1",WS_CHILD|WS_VISIBLE|WS_BORDER,82,18,180,28,win,nullptr,h,nullptr);
 CreateWindowW(L"STATIC",L"Name",WS_CHILD|WS_VISIBLE,278,22,50,24,win,nullptr,h,nullptr);g_name=CreateWindowW(L"EDIT",L"Player",WS_CHILD|WS_VISIBLE|WS_BORDER,328,18,150,28,win,nullptr,h,nullptr);
 CreateWindowW(L"STATIC",L"Room",WS_CHILD|WS_VISIBLE,494,22,45,24,win,nullptr,h,nullptr);g_room=CreateWindowW(L"EDIT",L"1",WS_CHILD|WS_VISIBLE|WS_BORDER,539,18,60,28,win,nullptr,h,nullptr);
 g_connect=CreateWindowW(L"BUTTON",L"Connect",WS_CHILD|WS_VISIBLE,620,17,110,32,win,(HMENU)1001,h,nullptr);
 g_mute=CreateWindowW(L"BUTTON",L"Mute",WS_CHILD|WS_VISIBLE,24,62,110,34,win,nullptr,h,nullptr);
 g_screen=CreateWindowW(L"BUTTON",L"Share Screen",WS_CHILD|WS_VISIBLE,146,62,140,34,win,(HMENU)1004,h,nullptr);
 g_status=CreateWindowW(L"STATIC",L"Not connected",WS_CHILD|WS_VISIBLE,310,68,420,28,win,nullptr,h,nullptr);
 g_preview=CreateWindowExW(WS_EX_CLIENTEDGE,L"VoxelPreview",L"",WS_CHILD|WS_VISIBLE,24,115,900,480,win,nullptr,h,nullptr);
 MSG msg;while(GetMessageW(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
}