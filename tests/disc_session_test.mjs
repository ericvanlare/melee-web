import assert from "node:assert/strict";
import {Blob, File} from "node:buffer";
import {basename} from "node:path";
import {openSync, closeSync, readSync, statSync} from "node:fs";

if (typeof globalThis.File !== "function") globalThis.File = File;

import {
  MAX_BUNDLE,
  MAX_FILE,
  DiscAssetSession,
  openDiscSession,
  preflightScope,
} from "../web/disc-session.mjs";
import {openNativeGameSession} from "../web/runtime-audio-assets.mjs";
import {AUDIO_FILTER_SHA256} from "../web/dsp-coefficients.mjs";

const entries = new Map([
  ["stage.dat", {offset: 0x1000, size: 12}],
  ["audio/main.ssm", {offset: 0x2000, size: 8}],
]);

// Pure preflight checks are intentionally independent of a disc image so a
// missing-path failure can prove that no payload read was attempted.
const plan = preflightScope(entries, {
  stage: "stage.dat",
  music: "audio/main.ssm",
});
assert.deepEqual(plan.map(item => item.name), ["stage", "music"]);
assert.equal(plan[0].size, 12);
assert.throws(
  () => preflightScope(entries, {stage: "stage.dat", missing: "missing.dat"}),
  /Required game data is missing: missing\.dat/,
);
assert.throws(
  () => preflightScope(new Map([["too-large.dat", {offset: 0, size: MAX_FILE + 1}]]), {x: "too-large.dat"}),
  /Invalid game file size/,
);
assert.throws(
  () => preflightScope(new Map([
    ["a.dat", {offset: 0, size: Math.floor(MAX_BUNDLE / 3)}],
    ["b.dat", {offset: 1, size: Math.floor(MAX_BUNDLE / 3)}],
    ["c.dat", {offset: 2, size: Math.floor(MAX_BUNDLE / 3) + 3}],
  ]), {a: "a.dat", b: "b.dat", c: "c.dat"}),
  /current import budget/,
);

await assert.rejects(
  openDiscSession(new Blob([new Uint8Array(4)])),
  /requires a local File/,
);
await assert.rejects(
  openDiscSession(new File([new Uint8Array(4)], "disc.rvz")),
  /RVZ is not supported/,
);
assert.throws(
  () => new DiscAssetSession(null, null, null, null, null),
  /must be opened from a local File/,
);

/** A lazy File facade lets the optional real-disc check avoid reading the CISO into memory. */
class PathFile extends File {
  #path;
  #length;
  ranges = [];
  delayMs = 0;

  constructor(path) {
    super([], basename(path));
    this.#path = path;
    this.#length = statSync(path).size;
  }

  get size() {
    return this.#length;
  }

  slice(start, end, type) {
    this.ranges.push([start, end]);
    const length = end - start;
    const fd = openSync(this.#path, "r");
    try {
      const bytes = Buffer.alloc(length);
      let offset = 0;
      while (offset < length) offset += readSync(fd, bytes, offset, length - offset, start + offset);
      const blob = new Blob([bytes], type === undefined ? {} : {type});
      if (this.delayMs <= 0) return blob;
      return {
        arrayBuffer: () => new Promise((resolve, reject) => {
          setTimeout(() => blob.arrayBuffer().then(resolve, reject), this.delayMs);
        }),
      };
    } finally {
      closeSync(fd);
    }
  }
}

const discPath = process.env.MELEE_DISC_PATH;
if (discPath) {
  const source = new PathFile(discPath);
  const session = await openDiscSession(source);
  const metadata = session.metadata();
  assert(metadata.fstFileCount > 0);
  assert.equal(metadata.generatedFont.name, "sislib_font.bin");
  assert.equal(metadata.generatedFont.source, "validated DOL fixed font range");

  let beforeRead = 0;
  const validatedReads = source.ranges.length;
  await assert.rejects(
    session.readScope({stage: "GrNBa.dat", missing: "__candidate_missing__.dat"}, {
      beforeRead: () => ++beforeRead,
    }),
    /Required game data is missing/,
  );
  assert.equal(beforeRead, 0, "missing scope must fail before any file payload read");
  assert.equal(source.ranges.length, validatedReads, "missing scope must not read a file payload");
  const selected = await session.readScope({common: "PlCo.dat"}, {
    beforeRead: ({index}) => assert.equal(index, 0),
  });
  assert(selected.get("common") instanceof Uint8Array);
  assert.equal(session.fontBytes().byteLength, metadata.generatedFont.size);
  session.close();
  session.close();
  await assert.rejects(session.readScope({common: "PlCo.dat"}), /Session is closed/);

  const callbackSource = new PathFile(discPath);
  const callbackSession = await openDiscSession(callbackSource);
  await assert.rejects(
    callbackSession.readScope({common: "PlCo.dat"}, {beforeRead: () => callbackSession.close()}),
    /Session is closed/,
  );

  const delayedSource = new PathFile(discPath);
  const delayedSession = await openDiscSession(delayedSource);
  delayedSource.delayMs = 20;
  const pendingRead = delayedSession.readScope({common: "PlCo.dat"});
  setTimeout(() => delayedSession.close(), 1);
  await assert.rejects(pendingRead, /Session is closed/);

  const progress = [];
  const audioSession = await openNativeGameSession(new PathFile(discPath));
  const audio = await audioSession.readScope(['PlCo.dat', 'sislib_font.bin', 'dsp_coef.bin'], event => progress.push(event));
  assert.deepEqual([...audio.keys()], ['PlCo.dat', 'sislib_font.bin', 'dsp_coef.bin']);
  assert.equal(progress.at(-1).complete, 3);
  await assert.rejects(audioSession.readScope(['PlCo.dat', '__missing__']), /Unknown native scene asset/);
  await assert.rejects(audioSession.readScope(['PlCo.dat', 'PlCo.dat']), /duplicate/);
  const second = await audioSession.readScope(['GrNLa.dat']);
  assert.deepEqual([...second.keys()], ['GrNLa.dat']);
  audioSession.close();
  await assert.rejects(audioSession.readScope(['PlCo.dat']), /Session is closed/);
  assert(audio.get("sislib_font.bin") instanceof Uint8Array);
  assert.equal(audio.get("dsp_coef.bin").byteLength, 4096);
  const digest = Array.from(
    new Uint8Array(await crypto.subtle.digest("SHA-256", audio.get("dsp_coef.bin"))),
    value => value.toString(16).padStart(2, "0"),
  ).join("");
  assert.equal(digest, AUDIO_FILTER_SHA256);
  console.log("Disc session real-file validation, preflight, close, font, and DSP checks passed");
} else {
  console.log("Disc session pure preflight/File-ownership checks passed (set MELEE_DISC_PATH for real-disc scope checks)");
}
