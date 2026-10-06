#pragma once
#include <string>
class ScreenCapture { public: bool capture_to_file(const std::wstring& path); std::wstring last_error()const{return error_;} private: std::wstring error_; };
