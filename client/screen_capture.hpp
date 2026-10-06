#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
struct CaptureSource { enum class Type { Screen, Window }; Type type{Type::Screen}; HMONITOR monitor{nullptr}; HWND window{nullptr}; std::wstring title; };
class ScreenCapture {
public:
 bool capture_jpeg(std::vector<uint8_t>& jpeg,const CaptureSource& source,int max_width=960,int max_height=540,int quality=55);
 bool capture_to_file(const std::wstring& path);
 static std::vector<CaptureSource> enumerate_sources();
 std::wstring last_error() const{return error_;}
private: std::wstring error_;
};