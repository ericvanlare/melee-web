#include "gameplay_replay_session.hpp"
#include "gameplay_replay_transport.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

std::string hex(const uint8_t* bytes, std::size_t size) {
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < size; ++index)
        result << std::setw(2) << unsigned(bytes[index]);
    return result.str();
}

melee_web::RuntimeFiles load_files(const char* menu_root,
                                   const char* game_root) {
    melee_web::RuntimeFiles files;
    for (const auto* root : {menu_root, game_root}) {
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.file_size() > 64 * 1024 * 1024)
                continue;
            std::ifstream stream(entry.path(), std::ios::binary);
            files[entry.path().filename().string()] = {
                std::istreambuf_iterator<char>(stream), {}};
        }
    }
    return files;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::runtime_error(
                "Expected owned menu/game directories and a v2 MWRP workload");
        const auto replay = melee_web::load_gameplay_replay_transport(argv[3]);
        auto files = load_files(argv[1], argv[2]);
        melee_web::GameplayReplaySession session(files, replay);
        while (!session.complete() && !session.failed())
            session.step();

        const bool complete = session.complete();
        const std::size_t frames_consumed = session.frames_consumed();
        const std::string failure = session.failure().value_or(
            "source workload did not complete");

        // Teardown is part of a successful workload. Do it before emitting the
        // success record so the record cannot claim completion for a leaked or
        // still-live source match.
        session.match().close();
        if (!complete) {
            std::cerr << failure << '\n';
            return 1;
        }

        std::cout << "{\"schema\":\"melee-web-replay-result\",\"schema_version\":2"
                  << ",\"source_sha256\":\""
                  << hex(replay.source_sha256.data(), replay.source_sha256.size())
                  << "\",\"frames_total\":" << replay.frames.size()
                  << ",\"frames_consumed\":" << frames_consumed
                  << ",\"source_stage\":" << replay.source_stage
                  << ",\"workload_stage\":" << replay.stage
                  << ",\"status\":\"workload_completed\""
                  << ",\"comparison\":\"not_run\""
                  << ",\"workload_profile\":\"derived_stock_no_timer_no_items\""
                  << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
