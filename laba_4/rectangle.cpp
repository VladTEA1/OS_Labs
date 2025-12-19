#include <iostream>

extern "C" float CalculateArea(float a, float b) {
    std::cout << "[Rectangle] Calculating area: " << a << " * " << b << std::endl;
    return a * b;
}
