#include <iostream>

extern "C" float CalculateArea(float a, float b) {
    std::cout << "[Triangle] Calculating area: (" << a << " * " << b << ") / 2" << std::endl;
    return (a * b) / 2.0f;
}