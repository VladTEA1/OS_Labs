#include <iostream>
#include <string>
#include <cstdio>
#include <dlfcn.h>

int main() {
    std::cout << "=== Dynamic Application (POSIX dlopen/dlsym/dlclose) ===" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  0      - Switch implementations" << std::endl;
    std::cout << "  1 a b  - Calculate area" << std::endl;
    std::cout << "  2 x    - Convert number" << std::endl;
    std::cout << "  exit   - Exit" << std::endl << std::endl;
    
    // Дескрипторы библиотек
    void* geom_lib = nullptr;
    void* conv_lib = nullptr;
    
    // Указатели на функции
    float (*CalculateArea)(float, float) = nullptr;
    const char* (*ConvertNumber)(long) = nullptr;
    
    // Флаги текущих реализаций
    bool is_rectangle = true;
    bool is_binary = true;
    
    // Первоначальная загрузка библиотек
    geom_lib = dlopen("./librectangle.so", RTLD_LAZY);
    conv_lib = dlopen("./libbinary.so", RTLD_LAZY);
    
    if (!geom_lib || !conv_lib) {
        std::cerr << "Error loading libraries: " << dlerror() << std::endl;
        return 1;
    }
    
    // Получаем указатели на функции
    CalculateArea = (float(*)(float,float)) dlsym(geom_lib, "CalculateArea");
    ConvertNumber = (const char*(*)(long)) dlsym(conv_lib, "ConvertNumber");
    
    if (!CalculateArea || !ConvertNumber) {
        std::cerr << "Error getting functions: " << dlerror() << std::endl;
        dlclose(geom_lib);
        dlclose(conv_lib);
        return 1;
    }
    
    std::cout << "Current: Rectangle, Binary" << std::endl << std::endl;
    
    while (true) {
        std::cout << "> ";
        std::string command;
        std::getline(std::cin, command);
        
        if (command == "exit") {
            std::cout << "Exiting..." << std::endl;
            break;
        }
        
        if (command == "0") {
            // Переключаем библиотеки
            
            // Закрываем текущие библиотеки
            dlclose(geom_lib);
            dlclose(conv_lib);
            
            // Загружаем новые библиотеки
            if (is_rectangle) {
                geom_lib = dlopen("./libtriangle.so", RTLD_LAZY);
                if (!geom_lib) {
                    std::cerr << "Error loading libtriangle.so: " << dlerror() << std::endl;
                    return 1;
                }
                std::cout << "Switched to: Triangle" << std::endl;
            } else {
                geom_lib = dlopen("./librectangle.so", RTLD_LAZY);
                if (!geom_lib) {
                    std::cerr << "Error loading librectangle.so: " << dlerror() << std::endl;
                    return 1;
                }
                std::cout << "Switched to: Rectangle" << std::endl;
            }
            
            if (is_binary) {
                conv_lib = dlopen("./libhex.so", RTLD_LAZY);
                if (!conv_lib) {
                    std::cerr << "Error loading libhex.so: " << dlerror() << std::endl;
                    return 1;
                }
                std::cout << "Switched to: Hexadecimal" << std::endl;
            } else {
                conv_lib = dlopen("./libbinary.so", RTLD_LAZY);
                if (!conv_lib) {
                    std::cerr << "Error loading libbinary.so: " << dlerror() << std::endl;
                    return 1;
                }
                std::cout << "Switched to: Binary" << std::endl;
            }
            
            // Обновляем указатели на функции
            dlerror(); // Очищаем предыдущие ошибки
            CalculateArea = (float(*)(float,float)) dlsym(geom_lib, "CalculateArea");
            ConvertNumber = (const char*(*)(long)) dlsym(conv_lib, "ConvertNumber");
            
            char* error = dlerror();
            if (error) {
                std::cerr << "Error getting functions after switch: " << error << std::endl;
                dlclose(geom_lib);
                dlclose(conv_lib);
                return 1;
            }
            
            // Меняем флаги
            is_rectangle = !is_rectangle;
            is_binary = !is_binary;
            std::cout << std::endl;
        }
        else if (!command.empty() && command[0] == '1') {
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
            std::cout << "Unknown command. Available: 0, 1 a b, 2 x, exit" << std::endl;
        }
    }
    
    // Закрываем библиотеки перед выходом
    if (geom_lib) dlclose(geom_lib);
    if (conv_lib) dlclose(conv_lib);
    
    return 0;
}