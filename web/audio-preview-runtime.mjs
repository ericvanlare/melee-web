/** Staging listening preview. The production player does not import this module. */
import {mountMeleeRuntime as mountPlayer} from './melee-runtime.mjs';
import {loadNativeGameDisc} from './runtime-audio-assets.mjs';
import {createRuntimeAudio} from './runtime-audio.mjs';

export const mountMeleeRuntime = options => mountPlayer({
  ...options,
  readDisc: loadNativeGameDisc,
  createAudio: createRuntimeAudio,
  loaderUrl: new URL('./gameplay_audio_preview.js', import.meta.url),
});
