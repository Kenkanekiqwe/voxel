#include "screen_capture.hpp"
#include <windows.h>
#include <wincodec.h>
#include <vector>
bool ScreenCapture::capture_to_file(const std::wstring& path){
 error_.clear();HDC screen=GetDC(nullptr);if(!screen){error_=L"GetDC failed";return false;}
 int w=GetSystemMetrics(SM_CXSCREEN),h=GetSystemMetrics(SM_CYSCREEN);HDC mem=CreateCompatibleDC(screen);HBITMAP bmp=CreateCompatibleBitmap(screen,w,h);
 if(!mem||!bmp){if(mem)DeleteDC(mem);ReleaseDC(nullptr,screen);error_=L"bitmap allocation failed";return false;}
 HGDIOBJ old=SelectObject(mem,bmp);BitBlt(mem,0,0,w,h,screen,0,0,SRCCOPY|CAPTUREBLT);SelectObject(mem,old);
 BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
 std::vector<unsigned char> pixels((size_t)w*h*4);GetDIBits(mem,bmp,0,h,pixels.data(),&bi,DIB_RGB_COLORS);
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);IWICImagingFactory*f=nullptr;HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f));
 if(SUCCEEDED(hr)){IWICBitmap*wb=nullptr;IWICStream*st=nullptr;IWICBitmapEncoder*en=nullptr;IWICBitmapFrameEncode*fr=nullptr;
  hr=f->CreateBitmapFromMemory(w,h,GUID_WICPixelFormat32bppBGRA,w*4,(UINT)pixels.size(),pixels.data(),&wb);
  if(SUCCEEDED(hr))hr=f->CreateStream(&st);if(SUCCEEDED(hr))hr=st->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
  if(SUCCEEDED(hr))hr=f->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,&en);if(SUCCEEDED(hr))hr=en->Initialize(st,WICBitmapEncoderNoCache);
  if(SUCCEEDED(hr))hr=en->CreateNewFrame(&fr,nullptr);if(SUCCEEDED(hr))hr=fr->Initialize(nullptr);if(SUCCEEDED(hr))hr=fr->SetSize(w,h);
  if(SUCCEEDED(hr))hr=fr->SetPixelFormat((WICPixelFormatGUID*)&GUID_WICPixelFormat32bppBGRA);if(SUCCEEDED(hr))hr=fr->WriteSource(wb,nullptr);
  if(SUCCEEDED(hr))hr=fr->Commit();if(SUCCEEDED(hr))hr=en->Commit();
  if(fr)fr->Release();if(en)en->Release();if(st)st->Release();if(wb)wb->Release();f->Release();
 }
 DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);if(FAILED(hr)){error_=L"WIC JPEG encode failed";return false;}return true;
}
