#include <iostream>
#include <sstream>
#include <iomanip>

extern "C" const char* ConvertNumber(long x) {
    static char result[20];
    std::stringstream ss;
    ss << std::hex << std::uppercase << x;
    std::string hexStr = ss.str();
    
    std::cout << "[Hex] Converting: " << x << std::endl;
    
    for (size_t i = 0; i < hexStr.length() && i < 19; i++) {
        result[i] = hexStr[i];
    }
    result[hexStr.length()] = '\0';
    
    return result;
}