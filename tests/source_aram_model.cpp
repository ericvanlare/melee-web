#include "source_aram_context.hpp"

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace melee_web::source_aram;

namespace {

std::uint64_t number(const char* text)
{
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed, 0);
    if (consumed != std::string(text).size())
        throw std::runtime_error("source amount is not numeric");
    return value;
}

std::uint32_t offset(Address address, std::uint32_t base)
{
    return address.value() ? address.value() - base : 0;
}

struct Driver {
    Context context;
    std::uint32_t base;
    std::uint32_t capacity;
    std::uint32_t hardware_size;
    std::map<std::string, Address> allocations;

    void emit(const std::string& op, Status status, Result result = {})
    {
        const auto state = context.snapshot();
        std::printf("{\"op\":\"%s\",\"status\":\"%s\"",
                    op.c_str(), status_name(status));
        if (result.status == Status::ok)
            std::printf(",\"address\":%u,\"size\":%u",
                        offset(result.address, base), result.size);
        std::printf(",\"initialized\":%s,\"stack\":%u,\"capacity\":%u,\"free_blocks\":%u,\"hardware_size\":%u,\"allocations\":[",
                    state.initialized ? "true" : "false",
                    offset(state.stack_pointer, base), state.capacity,
                    state.free_blocks,
                    state.hardware_size ? state.hardware_size - base : 0);
        for (std::size_t i = 0; i < state.allocations.size(); ++i) {
            const auto& allocation = state.allocations[i];
            std::printf("%s{\"address\":%u,\"size\":%u}",
                        i ? "," : "", offset(allocation.address, base),
                        allocation.size);
        }
        std::puts("]}");
        std::fflush(stdout);
    }

    void run(const std::string& line)
    {
        char op = 0;
        char first[80] = {};
        char second[80] = {};
        const int count = std::sscanf(line.c_str(), " %c %79s %79s",
                                      &op, first, second);
        if (count < 1 || op == '#') return;
        if (op == 'i') {
            allocations.clear();
            emit("init", context.initialize({Address(base), capacity, hardware_size}));
            return;
        }
        if (op == 'a') {
            if (count != 3) throw std::runtime_error("alloc expects label size");
            const auto result = context.allocate(number(second));
            if (result.status == Status::ok) allocations[first] = result.address;
            emit("alloc", result.status, result);
            return;
        }
        if (op == 'f') {
            const auto result = context.free();
            emit("free", result.status, result);
            return;
        }
        if (op == 'g') {
            emit("get_size", context.initialized() ? Status::ok : Status::missing_context);
            return;
        }
        if (op == 'c') {
            context.clear();
            allocations.clear();
            emit("clear", Status::ok);
            return;
        }
        throw std::runtime_error("unknown source-aram operation");
    }
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    try {
        Driver driver{Context{}, static_cast<std::uint32_t>(number(argv[1])),
                      static_cast<std::uint32_t>(number(argv[2])),
                      static_cast<std::uint32_t>(number(argv[3])), {}};
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty()) driver.run(line);
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
}
