#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_bank_transport.h"
#include "gameplay_audio_residency.h"
#include "gameplay_bootstrap.h"
#include "hsd_native_joint.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
int melee_web_test_audio_start(void);
void melee_web_test_audio_request_css_banks(void);
void melee_web_test_audio_request_mario_bank(void);
void melee_web_test_audio_wait_for_banks(void);
int melee_web_test_audio_pending_loads(void);
void melee_web_test_audio_cancel_or_shutdown(void);
int lbAudioAx_800237A8(int, int, int);
int lbAudioAx_80023694(void);
}

static std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Required audio fixture is unavailable: " +
                                 path.string());
    }
    return {std::istreambuf_iterator<char>(file), {}};
}

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct RenderObservation {
    double energy;
    uint32_t sample_id;
};

static RenderObservation render_original_sound(MeleeWebAudio* audio,
                                               int sound_id,
                                               uint32_t expected_first,
                                               uint32_t expected_last,
                                               const char* label)
{
    char error[256];
    require(lbAudioAx_800237A8(sound_id, 127, 64) >= 0,
            std::string("Original AX rejected ") + label);

    float pcm[160 * 2];
    bool found = false;
    uint32_t found_id = UINT32_MAX;
    double energy = 0.0;

    /* Render complete original 160-sample blocks so the source AX callback,
     * dynamic address binding, and PCM publication all run. */
    for (unsigned block = 0; block < 20; ++block) {
        require(melee_web_audio_render(audio, pcm, 160, error, sizeof(error)),
                std::string("Native PCM render failed for ") + label + ": " +
                    error);
        for (float value : pcm) {
            require(std::isfinite(value), std::string("Non-finite PCM for ") +
                                             label);
            energy += double(value) * double(value);
        }

        uint32_t ids[64];
        int count = melee_web_audio_active_samples(audio, ids, 64);
        require(count >= 0, std::string("Active sample observer failed for ") +
                              label);
        for (int i = 0; i < count; ++i) {
            if (ids[i] >= expected_first && ids[i] < expected_last) {
                found = true;
                found_id = ids[i];
            }
        }
    }

    require(found, std::string("Original ") + label +
                       " did not publish a sample in the expected SSM bank");
    require(energy > 1.0e-8, std::string("Original ") + label +
                                  " produced zero PCM energy");
    std::cout << label << " published sample " << found_id << " energy "
              << energy << "\n";
    return {energy, found_id};
}

static void register_source_banks(
    MeleeWebAudioResidency* registry,
    const std::vector<std::vector<uint8_t>>& bytes,
    const std::vector<std::string>& paths)
{
    char error[256];
    for (size_t i = 0; i < bytes.size(); ++i) {
        MeleeWebAudioResidencyAsset asset = {
            paths[i].c_str(), bytes[i].data(), bytes[i].size(), 100 + int(i)};
        require(melee_web_audio_residency_register(registry, &asset, error,
                                                   sizeof(error)),
                error);
    }
}

static void run_audio_world(
    const std::filesystem::path& menu_dir,
    const std::filesystem::path& audio_dir,
    unsigned cycle)
{
    const char* names[] = {
        "main.ssm",   "mario.ssm",   "nr_select.ssm", "nr_title.ssm",
        "nr_name.ssm", "pokemon.ssm", "end.ssm",
    };
    std::vector<std::vector<uint8_t>> bytes;
    std::vector<std::string> paths;
    bytes.reserve(7);
    paths.reserve(7);
    for (unsigned i = 0; i < 7; ++i) {
        const bool base_audio = i < 2;
        bytes.push_back(read_file((base_audio ? audio_dir : menu_dir) / names[i]));
        paths.emplace_back(std::string("/audio/us/") + names[i]);
    }

    const auto sem = read_file(audio_dir / "smash2.sem");
    const auto coefficients = read_file(audio_dir / "dsp_coef.bin");
    std::vector<std::span<const uint8_t>> bank_views;
    bank_views.reserve(bytes.size());
    for (const auto& bank : bytes) {
        bank_views.emplace_back(bank);
    }

    char error[256];
    bool world = false;
    bool transport = false;
    MeleeWebAudioResidency* registry = nullptr;

    /* The SDK heap must exist before GameplayAudioBank publishes native
     * synth/AX state. It owns all decoded PCM and SEM words until after
     * transport_end; the original synth remains the only bank scheduler. */
    std::unique_ptr<melee_web::GameplayAudioBank> audio;
    try {
        require(melee_web_gameplay_startup(32 * 1024 * 1024, error,
                                           sizeof(error)),
                error);
        world = true;
        require(melee_web_native_world_enable(error, sizeof(error)), error);
        audio = std::make_unique<melee_web::GameplayAudioBank>(
            sem, bank_views, coefficients);

        registry = melee_web_audio_residency_create(error, sizeof(error));
        require(registry != nullptr, error);
        register_source_banks(registry, bytes, paths);

        require(melee_web_audio_bank_transport_begin(
                    audio->get(), registry, "/audio/us/smash2.sem", error,
                    sizeof(error)),
                error);
        transport = true;

        require(melee_web_test_audio_start() != 0,
                "Original lbaudio startup did not run");
        require(melee_web_audio_bank_transport_active(),
                "Source SSM transport did not become active");

        /* Request the exact CSS enter bank set, then cancel before the
         * original wait. This proves fn_800269AC/HSD_SynthSFXCancelLoad reach
         * the real transport rather than a fabricated completion callback. */
        melee_web_test_audio_request_css_banks();
        const int pending_before_cancel = melee_web_test_audio_pending_loads();
        require(pending_before_cancel > 0,
                "CSS SSM request completed before cancellation could run");
        melee_web_test_audio_cancel_or_shutdown();
        require(melee_web_test_audio_pending_loads() == 0,
                "Original SSM cancellation left a pending request");

        /* Reload the exact CSS set and wait through the original callback
         * path, then play mnhyaku's source nr_select program 0x7530. */
        melee_web_test_audio_request_css_banks();
        melee_web_test_audio_wait_for_banks();
        require(melee_web_test_audio_pending_loads() == 0,
                "CSS SSM wait returned with pending requests");
        render_original_sound(audio->get(), 0x7530, 295, 318, "nr_select");

        /* Exercise the exact character switch path used by CSS OnExit with
         * the local Mario bank, including original deflag/compaction logic. */
        lbAudioAx_80023694();
        melee_web_test_audio_request_mario_bank();
        melee_web_test_audio_wait_for_banks();
        require(melee_web_test_audio_pending_loads() == 0,
                "Mario SSM wait returned with pending requests");
        render_original_sound(audio->get(), 180000, 783, 815, "mario");

        /* Switch back to the CSS bank set to exercise another original
         * flag/switch transition before teardown. */
        lbAudioAx_80023694();
        melee_web_test_audio_request_css_banks();
        melee_web_test_audio_wait_for_banks();
        require(melee_web_test_audio_pending_loads() == 0,
                "CSS return wait returned with pending requests");

        require(melee_web_audio_bank_transport_end(error, sizeof(error)),
                error);
        transport = false;
        require(!melee_web_audio_bank_transport_active(),
                "Source SSM transport remained active after closure");
        audio.reset();

        require(melee_web_audio_residency_destroy(registry, error,
                                                  sizeof(error)),
                error);
        registry = nullptr;

        require(melee_web_gameplay_shutdown(error, sizeof(error)), error);
        world = false;
        std::cout << "audio world " << cycle << " closed after original "
                  << "SSM callbacks and PCM publication\n";
    } catch (...) {
        if (transport) {
            melee_web_test_audio_cancel_or_shutdown();
            melee_web_audio_bank_transport_end(error, sizeof(error));
        }
        if (audio) {
            audio.reset();
        }
        if (registry) {
            melee_web_audio_residency_destroy(registry, error, sizeof(error));
        }
        if (world) {
            melee_web_gameplay_shutdown(error, sizeof(error));
        }
        throw;
    }
}

int main(int argc, char** argv)
{
    try {
        require(argc == 3,
                "Expected native menu asset directory and base audio directory");
        const std::filesystem::path menu_dir = argv[1];
        const std::filesystem::path audio_dir = argv[2];

        for (unsigned cycle = 0; cycle < 2; ++cycle) {
            run_audio_world(menu_dir, audio_dir, cycle);
        }

        std::cout << "Original SSM bank startup/menu flag-switch/cancel/PCM/"
                     "closure passed in two worlds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
