#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

#include <notcurses/notcurses.h>

#include "animations/band/sprite_types.h"

using namespace std::chrono_literals;

namespace {
constexpr float kDeltaSeconds = 1.0f / 60.0f;
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    using when::animations::band::SpritePlayer;
    using when::animations::band::SpriteSequence;
    using when::animations::band::load_sprite_sequence_from_directory;

    fs::path assets_root = "assets/sprites";
    if (argc > 1) {
        assets_root = argv[1];
    }

    try {
        SpriteSequence sequence = load_sprite_sequence_from_directory(assets_root / "guitarist");
        if (sequence.empty()) {
            fprintf(stderr, "No frames loaded for guitarist sequence\n");
            return 1;
        }

        notcurses_options opts{};
        opts.flags = NCOPTION_NO_ALTERNATE_SCREEN;
        struct notcurses* nc = notcurses_init(&opts, nullptr);
        if (!nc) {
            fprintf(stderr, "Failed to init notcurses\n");
            return 1;
        }

        ncplane* stdplane = notcurses_stddim_yx(nc, nullptr, nullptr);
        SpritePlayer player;
        player.set_sequence(&sequence);
        player.set_fps(6.0f);

        bool running = true;
        float elapsed = 0.0f;
        while (running) {
            player.update(kDeltaSeconds, 0.0f, 0.0f);
            const auto& frame = player.current();

            ncplane_erase(stdplane);
            int y = 0;
            for (const auto& row : frame.rows) {
                ncplane_putstr_yx(stdplane, y, 0, row.c_str());
                ++y;
            }

            notcurses_render(nc);
            std::this_thread::sleep_for(16ms);

            elapsed += kDeltaSeconds;
            if (elapsed > 30.0f) {
                running = false;
            }
        }

        notcurses_stop(nc);
    } catch (const std::exception& ex) {
        fprintf(stderr, "Error: %s\n", ex.what());
        return 1;
    }

    return 0;
}
