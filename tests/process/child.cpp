#include <chrono>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const std::string mode = argv[1];
    if (mode == "idle") {
        std::this_thread::sleep_for(std::chrono::seconds(6));
        return 0;
    }
    if (mode == "env") {
        if (const char* value = std::getenv("LTOOL_TEST_PROC_VAR")) std::cout << value;
        return 0;
    }
    if (mode == "duplex") {
        std::cerr << std::string(131072, 'e') << std::flush;
        std::cout << std::string(131072, 'o') << std::flush;
        std::cout << std::cin.rdbuf();
        return 0;
    }
    return 42;
}
