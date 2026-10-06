#pragma once
#include <cstdint>
#include <string>
#include <vector>
class ScreenCapture {
public:
 bool capture_jpeg(std::vector<uint8_t>& jpeg, int max_width=960, int max_height=540, int quality=55);
 bool capture_to_file(const std::wstring& path);
 std::wstring last_error()const{return error_;}
private:
 std::wstring error_;
};