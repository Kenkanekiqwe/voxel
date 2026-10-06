#include "screen_capture.hpp"
#include <windows.h>
#include <wincodec.h>
#include <vector>
#include <algorithm>
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")

static void release_unknown(IUnknown* p){ if(p) p->Release(); }

bool ScreenCapture::capture_jpeg(std::vector<uint8_t>& jpeg,const CaptureSource& capture_source,int max_width,int max_height,int quality){
 jpeg.clear(); error_.clear(); (void)quality;
 HDC dc=nullptr; int sw=0,sh=0; POINT origin{0,0};
 if(capture_source.type==CaptureSource::Type::Window && capture_source.window && IsWindow(capture_source.window)){
  RECT r{}; GetWindowRect(capture_source.window,&r); sw=r.right-r.left; sh=r.bottom-r.top; dc=GetDC(capture_source.window);
 } else {
  HMONITOR mon=capture_source.monitor?capture_source.monitor:MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(mon,&mi); sw=mi.rcMonitor.right-mi.rcMonitor.left; sh=mi.rcMonitor.bottom-mi.rcMonitor.top; origin={mi.rcMonitor.left,mi.rcMonitor.top}; dc=GetDC(nullptr);
 }
 auto release_dc=[&](){if(dc) ReleaseDC(capture_source.type==CaptureSource::Type::Window?capture_source.window:nullptr,dc);};
 if(!dc||sw<=0||sh<=0){release_dc();error_=L"Capture source unavailable";return false;}
 HDC mem=CreateCompatibleDC(dc); HBITMAP bmp=CreateCompatibleBitmap(dc,sw,sh);
 if(!mem||!bmp){if(mem)DeleteDC(mem);if(bmp)DeleteObject(bmp);release_dc();error_=L"Bitmap allocation failed";return false;}
 HGDIOBJ old=SelectObject(mem,bmp);
 BOOL copied=BitBlt(mem,0,0,sw,sh,dc,capture_source.type==CaptureSource::Type::Window?0:origin.x,capture_source.type==CaptureSource::Type::Window?0:origin.y,SRCCOPY|CAPTUREBLT);
 SelectObject(mem,old);
 if(!copied){DeleteObject(bmp);DeleteDC(mem);release_dc();error_=L"BitBlt failed";return false;}
 IWICImagingFactory* factory=nullptr; IWICBitmap* wic_bitmap=nullptr; IWICBitmapScaler* scaler=nullptr; IWICStream* stream=nullptr; IPropertyBag2* props=nullptr;
 HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
 if(FAILED(hr)){DeleteObject(bmp);DeleteDC(mem);release_dc();return false;}
 BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=sw;bi.bmiHeader.biHeight=-sh;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
 std::vector<BYTE> pixels((size_t)sw*sh*4); GetDIBits(mem,bmp,0,(UINT)sh,pixels.data(),&bi,DIB_RGB_COLORS);
 factory->CreateBitmapFromMemory((UINT)sw,(UINT)sh,GUID_WICPixelFormat32bppBGRA,(UINT)sw*4,(UINT)pixels.size(),pixels.data(),&wic_bitmap);
 DeleteObject(bmp);DeleteDC(mem);release_dc();
 if(!wic_bitmap){factory->Release();return false;}
 IWICBitmapSource* final_source=wic_bitmap; UINT outw=sw,outh=sh;
 double scale=std::min(1.0,std::min((double)max_width/sw,(double)max_height/sh));
 if(scale<1.0){outw=(UINT)(sw*scale);outh=(UINT)(sh*scale);factory->CreateBitmapScaler(&scaler);if(!scaler||FAILED(scaler->Initialize(wic_bitmap,outw,outh,WICBitmapInterpolationModeFant))){wic_bitmap->Release();if(scaler)scaler->Release();factory->Release();return false;}final_source=scaler;}
 factory->CreateStream(&stream); HGLOBAL hg=GlobalAlloc(GMEM_MOVEABLE,0); (void)hg;
 IStream* memstream=nullptr; CreateStreamOnHGlobal(nullptr,TRUE,&memstream);
 IWICBitmapEncoder* enc=nullptr; IWICBitmapFrameEncode* frame=nullptr; factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,&enc);
 if(!enc||!memstream||FAILED(enc->Initialize(memstream,WICBitmapEncoderNoCache))){if(enc)enc->Release();if(memstream)memstream->Release();if(stream)stream->Release();if(scaler)scaler->Release();wic_bitmap->Release();factory->Release();return false;}
 enc->CreateNewFrame(&frame,&props); if(frame){frame->Initialize(props);frame->SetSize(outw,outh);WICPixelFormatGUID pf=GUID_WICPixelFormat24bppBGR;frame->SetPixelFormat(&pf);frame->WriteSource(final_source,nullptr);frame->Commit();props->Release();frame->Release();} enc->Commit();
 STATSTG st{};memstream->Stat(&st,STATFLAG_NONAME);ULONG size=(ULONG)st.cbSize.QuadPart;LARGE_INTEGER zero{};memstream->Seek(zero,STREAM_SEEK_SET,nullptr);jpeg.resize(size);ULONG read=0;memstream->Read(jpeg.data(),size,&read);jpeg.resize(read);
 enc->Release();memstream->Release();if(stream)stream->Release();if(scaler)scaler->Release();wic_bitmap->Release();factory->Release();return !jpeg.empty();
}

std::vector<CaptureSource> ScreenCapture::enumerate_sources(){
 std::vector<CaptureSource> out;
 EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR mon,HDC,LPRECT,LPARAM p)->BOOL{ auto* v=(std::vector<CaptureSource>*)p; MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(mon,&mi); wchar_t label[80]; swprintf_s(label,L"Screen %zu (%ld x %ld)",v->size()+1,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top); v->push_back({CaptureSource::Type::Screen,mon,nullptr,label}); return TRUE; },(LPARAM)&out);
 EnumWindows([](HWND w,LPARAM p)->BOOL{ if(!IsWindowVisible(w)||IsIconic(w)||GetWindow(w,GW_OWNER)) return TRUE; wchar_t title[256]; GetWindowTextW(w,title,256); if(!title[0]) return TRUE; auto* v=(std::vector<CaptureSource>*)p; v->push_back({CaptureSource::Type::Window,nullptr,w,title}); return TRUE; },(LPARAM)&out);
 return out;
}

bool ScreenCapture::capture_to_file(const std::wstring& path){
 std::vector<uint8_t> jpeg; auto sources=enumerate_sources(); if(sources.empty() || !capture_jpeg(jpeg,sources.front())) return false;
 HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE){error_=L"CreateFile failed";return false;}
 DWORD written=0; BOOL ok=WriteFile(h,jpeg.data(),(DWORD)jpeg.size(),&written,nullptr);CloseHandle(h);
 if(!ok || written!=jpeg.size()){error_=L"WriteFile failed";return false;} return true;
}