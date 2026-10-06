#include "screen_capture.hpp"
#include <windows.h>
#include <wincodec.h>
#include <vector>
#include <algorithm>
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")

static void release_unknown(IUnknown* p){ if(p) p->Release(); }

bool ScreenCapture::capture_jpeg(std::vector<uint8_t>& jpeg,int max_width,int max_height,int quality){
 jpeg.clear(); error_.clear(); (void)quality;
 HDC screen=GetDC(nullptr);
 if(!screen){error_=L"GetDC failed";return false;}
 int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
 HDC mem=CreateCompatibleDC(screen); HBITMAP bmp=CreateCompatibleBitmap(screen,sw,sh);
 if(!mem||!bmp){if(mem)DeleteDC(mem);if(bmp)DeleteObject(bmp);ReleaseDC(nullptr,screen);error_=L"bitmap allocation failed";return false;}
 HGDIOBJ old=SelectObject(mem,bmp);
 BOOL copied=BitBlt(mem,0,0,sw,sh,screen,0,0,SRCCOPY|CAPTUREBLT);
 SelectObject(mem,old);
 if(!copied){DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);error_=L"BitBlt failed";return false;}

 BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=sw; bi.bmiHeader.biHeight=-sh;
 bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
 std::vector<uint8_t> pixels((size_t)sw*sh*4);
 if(!GetDIBits(mem,bmp,0,sh,pixels.data(),&bi,DIB_RGB_COLORS)){
  DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);error_=L"GetDIBits failed";return false;
 }
 DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);

 HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 bool uninit=SUCCEEDED(init);
 IWICImagingFactory* factory=nullptr; IWICBitmap* source=nullptr; IWICBitmap* scaled=nullptr;
 IWICBitmapEncoder* encoder=nullptr; IWICBitmapFrameEncode* frame=nullptr;
 HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
 if(SUCCEEDED(hr)) hr=factory->CreateBitmapFromMemory(sw,sh,GUID_WICPixelFormat32bppBGRA,sw*4,(UINT)pixels.size(),pixels.data(),&source);
 int dw=sw, dh=sh;
 if(dw>max_width || dh>max_height){double scale=std::min((double)max_width/dw,(double)max_height/dh);dw=std::max(1,(int)(dw*scale));dh=std::max(1,(int)(dh*scale));}
 if(SUCCEEDED(hr) && (dw!=sw||dh!=sh)) hr=factory->CreateBitmapScaler(&scaled);
 if(SUCCEEDED(hr) && scaled) hr=scaled->Initialize(source,dw,dh,WICBitmapInterpolationModeFant);
 IStream* memstream=nullptr;
 if(SUCCEEDED(hr)) hr=CreateStreamOnHGlobal(nullptr,TRUE,&memstream);
 if(SUCCEEDED(hr)) hr=factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,&encoder);
 if(SUCCEEDED(hr)) hr=encoder->Initialize(memstream,WICBitmapEncoderNoCache);
 if(SUCCEEDED(hr)) hr=encoder->CreateNewFrame(&frame,nullptr);
 if(SUCCEEDED(hr)) hr=frame->Initialize(nullptr);
 if(SUCCEEDED(hr)) hr=frame->SetSize(dw,dh);
 WICPixelFormatGUID pf=GUID_WICPixelFormat24bppBGR;
 if(SUCCEEDED(hr)) hr=frame->SetPixelFormat(&pf);
 if(SUCCEEDED(hr)){ PROPBAG2 bag{}; VARIANT v{}; v.vt=VT_UI1; v.bVal=(BYTE)std::clamp(quality,1,100); hr=frame->GetMetadataQueryWriter(nullptr); VariantClear(&v); (void)bag; }
 if(SUCCEEDED(hr)) hr=frame->WriteSource(scaled?static_cast<IWICBitmapSource*>(scaled):static_cast<IWICBitmapSource*>(source),nullptr);
 if(SUCCEEDED(hr)) hr=frame->Commit();
 if(SUCCEEDED(hr)) hr=encoder->Commit();
 if(SUCCEEDED(hr)){
  STATSTG stat{}; hr=memstream->Stat(&stat,STATFLAG_NONAME);
  if(SUCCEEDED(hr) && stat.cbSize.QuadPart>0 && stat.cbSize.QuadPart<10*1024*1024){
   LARGE_INTEGER zero{}; memstream->Seek(zero,STREAM_SEEK_SET,nullptr);
   jpeg.resize((size_t)stat.cbSize.QuadPart); ULONG read=0;
   hr=memstream->Read(jpeg.data(),(ULONG)jpeg.size(),&read); if(SUCCEEDED(hr)) jpeg.resize(read);
  }
 }
 if(frame)frame->Release(); if(encoder)encoder->Release(); if(memstream)memstream->Release();
 if(scaled)scaled->Release(); if(source)source->Release(); if(factory)factory->Release(); if(uninit)CoUninitialize();
 if(FAILED(hr)){jpeg.clear();error_=L"WIC JPEG capture failed";return false;} return true;
}

bool ScreenCapture::capture_to_file(const std::wstring& path){
 std::vector<uint8_t> jpeg; if(!capture_jpeg(jpeg)) return false;
 HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE){error_=L"CreateFile failed";return false;}
 DWORD written=0; BOOL ok=WriteFile(h,jpeg.data(),(DWORD)jpeg.size(),&written,nullptr);CloseHandle(h);
 if(!ok || written!=jpeg.size()){error_=L"WriteFile failed";return false;} return true;
}