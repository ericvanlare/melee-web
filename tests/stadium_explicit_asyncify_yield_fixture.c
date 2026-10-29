#include <emscripten.h>

static int retired;

EM_JS(void, install_asyncify_snapshot, (), {
  Module.stadiumAsyncifySnapshot = () => ({
    normal: Asyncify.state === Asyncify.State.Normal,
    curr_data_present: Asyncify.currData !== null,
    aborted: ABORT,
  });
  Module.stadiumMatchedSetupNativeIdle = () =>
    !ABORT && Asyncify.state === Asyncify.State.Normal && Asyncify.currData === null;
});

EMSCRIPTEN_KEEPALIVE void stadium_asyncify_fixture_install(void) {
  retired = 0;
  install_asyncify_snapshot();
}

EMSCRIPTEN_KEEPALIVE int stadium_asyncify_fixture_retire(void) {
  emscripten_sleep(0);
  retired = 1;
  return 1;
}

EMSCRIPTEN_KEEPALIVE int stadium_asyncify_fixture_retirement_status(void) {
  return retired;
}
