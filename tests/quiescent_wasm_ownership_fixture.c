#include <emscripten.h>
#include <emscripten/em_macros.h>

EM_JS_DEPS(wasm_ownership_fixture_private_library_dep,
           "$MeleeWebWasmOwnershipPrivateV1");

static volatile int fixture_phase;

EMSCRIPTEN_KEEPALIVE int wasm_ownership_fixture_phase(void) {
  return fixture_phase;
}

EMSCRIPTEN_KEEPALIVE int wasm_ownership_fixture_ping(void) {
  return fixture_phase + 1;
}

int main(void) {
  EM_ASM({ Module.__wasmOwnershipFixtureMainReached = true; });
  fixture_phase = 1;
  emscripten_sleep(2);
  fixture_phase = 2;
  return 0;
}
