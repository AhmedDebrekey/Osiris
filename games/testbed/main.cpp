#include "../../engine/core/Engine.h"
#include <iostream>

int main() {
    std::cout << "Osiris Engine" << std::endl;
    Osiris::Engine engine;
    engine.Initialize();
    engine.Run();
    engine.Shutdown();
    return 0;
}