#include <cstdlib>
#include <iostream>

#include <dlfcn.h>

namespace {
bool has_symbol(void* handle, const char* name) {
    dlerror();
    void* symbol = dlsym(handle, name);
    return symbol != nullptr && dlerror() == nullptr;
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "expected shared library path argument\n";
        return EXIT_FAILURE;
    }

    void* handle = dlopen(argv[1], RTLD_LAZY);
    if (!handle) {
        std::cerr << "failed to open plugin library: " << dlerror() << '\n';
        return EXIT_FAILURE;
    }

    const char* required_symbols[] = {
        "PLUGIN_API_VERSION",
        "PLUGIN_INIT",
        "PLUGIN_EXIT",
    };

    for (const char* symbol_name : required_symbols) {
        if (!has_symbol(handle, symbol_name)) {
            std::cerr << "missing symbol: " << symbol_name << '\n';
            dlclose(handle);
            return EXIT_FAILURE;
        }
    }

    dlclose(handle);
    return EXIT_SUCCESS;
}
