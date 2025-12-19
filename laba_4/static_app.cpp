#include <iostream>
#include <string>
#include <cstdio>
#include <dlfcn.h>

// Встроенные функции (имитация статической линковки)
float CalculateArea(float a, float b) {
    std::cout << "[Static Rectangle] Area calculation" << std::endl;
    return a * b;
}

const char* ConvertNumber(long x) {
    static char result[65];
    std::cout << "[Static Binary] Conversion" << std::endl;
    
    // Простой перевод в двоичную
    long n = x;
    std::string binary;
    
    if (n == 0) binary = "0";
    
    while (n > 0) {
        binary = (n % 2 == 0 ? "0" : "1") + binary;
        n /= 2;
    }
    
    for (size_t i = 0; i < binary.length() && i < 64; i++) {
        result[i] = binary[i];
    }
    result[binary.length()] = '\0';
    
    return result;
}

int main() {
    std::cout << "=== Static Application (built-in functions) ===" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  1 a b  - Calculate area (rectangle)" << std::endl;
    std::cout << "  2 x    - Convert to binary" << std::endl;
    std::cout << "  0      - Exit" << std::endl << std::endl;
    
    while (true) {
        std::cout << "> ";
        std::string command;
        std::getline(std::cin, command);
        
        if (command == "0") break;
        
        if (!command.empty() && command[0] == '1') {
            float a, b;
            if (std::sscanf(command.c_str(), "1 %f %f", &a, &b) == 2) {
                std::cout << "Result: " << CalculateArea(a, b) << std::endl;
            } else {
                std::cout << "Error: Invalid arguments. Use: 1 a b" << std::endl;
            }
        }
        else if (!command.empty() && command[0] == '2') {
            long x;
            if (std::sscanf(command.c_str(), "2 %ld", &x) == 1) {
                std::cout << "Result: " << ConvertNumber(x) << std::endl;
            } else {
                std::cout << "Error: Invalid arguments. Use: 2 x" << std::endl;
            }
        }
        else {
            std::cout << "Unknown command. Available: 0, 1 a b, 2 x" << std::endl;
        }
    }
    
    return 0;
}