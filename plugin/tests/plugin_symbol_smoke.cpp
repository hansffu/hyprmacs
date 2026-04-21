#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "expected shared library path argument\n";
        return EXIT_FAILURE;
    }

    const std::string command = std::string("nm -D --defined-only ") + argv[1];
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        std::cerr << "failed to run symbol inspection command\n";
        return EXIT_FAILURE;
    }

    std::unordered_set<std::string> exported_symbols;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::istringstream line(buffer);
        std::string address;
        std::string type;
        std::string symbol;
        if (!(line >> address >> type >> symbol)) {
            continue;
        }
        exported_symbols.insert(symbol);
    }

    const int rc = pclose(pipe);
    if (rc != 0) {
        std::cerr << "symbol inspection command failed with exit code " << rc << '\n';
        return EXIT_FAILURE;
    }

    const char* required_symbols[] = {
        "pluginAPIVersion",
        "pluginInit",
        "pluginExit",
    };

    for (const char* symbol_name : required_symbols) {
        if (!exported_symbols.contains(symbol_name)) {
            std::cerr << "missing symbol: " << symbol_name << '\n';
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
