#pragma once

#include "gameplay_audio.h"
#include "gameplay_world.hpp"

namespace melee_web {

enum class GameplayMenuScene { Characters, Stages };

// Owns the native services shared by the original CSS and SSS scenes. Source
// scene entry, input, ticking, rendering, and exit remain the caller's
// responsibility; this object only establishes the archive/audio/SDK lifetime
// that those source functions require.
class GameplayMenuWorld {
public:
    explicit GameplayMenuWorld(const RuntimeFiles&);
    GameplayMenuWorld(const RuntimeFiles&, RuntimeArchiveCache&);
    ~GameplayMenuWorld();

    GameplayMenuWorld(const GameplayMenuWorld&) = delete;
    GameplayMenuWorld& operator=(const GameplayMenuWorld&) = delete;

    // Call after the source scene's OnExit. The operation is idempotent.
    void close();

    // Release a prepared owner that has never entered a source CSS/SSS scene.
    // This deliberately skips source scene card/audio stop callbacks.
    void close_prepared();

    // Call after source OnExit when moving between CSS and SSS. Rebuilds all
    // mutable scene/HSD state while retaining the original menu audio scope.
    void rebuild_scene(GameplayMenuScene);

    // Browser preparation may yield between teardown and hydration so neither
    // half monopolizes a presentation callback. These two calls are the exact
    // same source lifecycle as rebuild_scene().
    void begin_scene_rebuild();
    void finish_scene_rebuild(GameplayMenuScene);

    [[nodiscard]] MeleeWebAudio* audio() const noexcept;
    void verify_immutable_archives() const;

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace melee_web
