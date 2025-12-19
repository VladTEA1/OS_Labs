#include <iostream>
#include <string>
#include <bitset>

extern "C" const char* ConvertNumber(long x) {
    static char result[65];
    std::string binary = std::bitset<64>(x).to_string();
    
    // Убираем ведущие нули
    size_t pos = binary.find('1');
    if (pos != std::string::npos) {
        binary = binary.substr(pos);
    } else {
        binary = "0";
    }
    
    std::cout << "[Binary] Converting: " << x << std::endl;
    
    for (size_t i = 0; i < binary.length() && i < 64; i++) {
        result[i] = binary[i];
    }
    result[binary.length()] = '\0';
    
    return result;
}