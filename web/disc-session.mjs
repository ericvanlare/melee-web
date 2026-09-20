import {
  fontFileRange,
  openDiscImage,
  relativeDiscPath,
} from "./disc-image.mjs";

// Pinned USA revision 1.02 executable identity shared by the compatibility loader.
export const ORIGINAL_DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45";

// These are the existing requested-source limits. They are deliberately kept
// here rather than inferred from a future scene descriptor.
export const MAX_FILE = 64 * 1024 * 1024;
export const MAX_BUNDLE = 128 * 1024 * 1024;
const SESSION_TOKEN = Symbol("validated DiscAssetSession");

async function sha1(bytes) {
  return Array.from(
    new Uint8Array(await crypto.subtle.digest("SHA-1", bytes)),
    value => value.toString(16).padStart(2, "0"),
  ).join("");
}

function requireLocalFile(file) {
  const FileConstructor = globalThis.File;
  if (typeof FileConstructor !== "function" || !(file instanceof FileConstructor)) {
    throw new TypeError("DiscAssetSession requires a local File");
  }
  if (/\.rvz$/i.test(file.name ?? "")) {
    throw new Error("RVZ is not supported yet. Choose an ISO, GCM, or CISO image.");
  }
}

function scopeEntries(scope) {
  if (scope instanceof Map) return [...scope.entries()];
  if (scope !== null && typeof scope === "object") return Object.entries(scope);
  throw new TypeError("disc asset scope must be a Map or object");
}

/**
 * Validate a complete logical-name -> FST-path scope without touching file
 * payloads. This is exported as a pure boundary check for focused tests.
 */
export function preflightScope(entries, scope) {
  const plan = [];
  const names = new Set();
  let totalBytes = 0;
  for (const [name, path] of scopeEntries(scope)) {
    if (typeof name !== "string" || !name || names.has(name)) {
      throw new Error("disc asset scope contains a duplicate or invalid logical name");
    }
    names.add(name);
    relativeDiscPath(path);
    const entry = entries.get(path);
    if (!entry) throw new Error(`Required game data is missing: ${path}`);
    if (!entry.size || entry.size > MAX_FILE) {
      throw new Error(`Invalid game file size: ${path}`);
    }
    totalBytes += entry.size;
    if (totalBytes > MAX_BUNDLE) {
      throw new Error("Required game data exceeds the current import budget.");
    }
    plan.push({name, path, offset: entry.offset, size: entry.size});
  }
  return plan.map(item => Object.freeze(item));
}

async function validateExecutable(disc) {
  const pointer = await disc.read(0x420, 4);
  const dolOffset = new DataView(pointer.buffer, pointer.byteOffset, 4).getUint32(0);
  if (dolOffset < 0x440) throw new Error("Invalid game executable location.");
  const header = await disc.read(dolOffset, 0x100);
  const view = new DataView(header.buffer, header.byteOffset, header.byteLength);
  let dolSize = 0x100;
  for (let index = 0; index < 18; ++index) {
    const offset = view.getUint32(index * 4);
    const size = view.getUint32(0x90 + index * 4);
    if (size) {
      if (offset < 0x100 || offset + size > MAX_FILE) {
        throw new Error("Invalid game executable section.");
      }
      dolSize = Math.max(dolSize, offset + size);
    }
  }
  const dol = await disc.read(dolOffset, dolSize);
  if (await sha1(dol) !== ORIGINAL_DOL_SHA1) {
    throw new Error("This build requires the unmodified USA revision 1.02 executable. This disc does not match.");
  }
  const font = fontFileRange(header);
  if (font.offset + font.size > dol.byteLength) {
    throw new Error("Font data is outside the validated executable.");
  }
  return {dolOffset, dolSize, header, dol, font};
}

/** A validated local disc whose FST and executable are immutable for its life. */
export class DiscAssetSession {
  #source;
  #disc;
  #entries;
  #executable;
  #closed = false;

  constructor(token, source, disc, entries, executable) {
    if (token !== SESSION_TOKEN) {
      throw new TypeError("DiscAssetSession must be opened from a local File");
    }
    this.#source = source;
    this.#disc = disc;
    this.#entries = entries;
    this.#executable = executable;
  }

  #assertOpen() {
    if (this.#closed) throw new Error("DiscAssetSession is closed");
  }

  /**
   * Preflight every path first, then read each complete file. A callback is
   * called only after the whole scope has passed preflight and immediately
   * before its corresponding payload read.
   */
  async readScope(scope, {beforeRead = () => {}} = {}) {
    this.#assertOpen();
    const plan = preflightScope(this.#entries, scope);
    this.#assertOpen();
    const result = new Map();
    for (let index = 0; index < plan.length; ++index) {
      const item = plan[index];
      this.#assertOpen();
      beforeRead({
        name: item.name,
        path: item.path,
        index,
        count: plan.length,
      });
      this.#assertOpen();
      const bytes = await this.#disc.read(item.offset, item.size);
      this.#assertOpen();
      result.set(item.name, bytes);
    }
    return result;
  }

  /** Return the source-derived font bytes, never an FST path. */
  fontBytes() {
    this.#assertOpen();
    const {font, dol} = this.#executable;
    return dol.slice(font.offset, font.offset + font.size);
  }

  metadata() {
    this.#assertOpen();
    return Object.freeze({
      sourceName: this.#source.name ?? "",
      dolOffset: this.#executable.dolOffset,
      dolSize: this.#executable.dolSize,
      fstFileCount: this.#entries.size,
      generatedFont: Object.freeze({
        name: "sislib_font.bin",
        source: "validated DOL fixed font range",
        size: this.#executable.font.size,
      }),
    });
  }

  /** Drop session references. Returned maps remain caller-owned byte copies. */
  close() {
    if (this.#closed) return;
    this.#closed = true;
    this.#source = null;
    this.#disc = null;
    this.#entries = null;
    this.#executable = null;
  }
}

/** Open and validate the executable and FST exactly once. */
export async function openDiscSession(file) {
  requireLocalFile(file);
  const disc = await openDiscImage(file);
  const executable = await validateExecutable(disc);
  const entries = await disc.files();
  return new DiscAssetSession(SESSION_TOKEN, file, disc, entries, executable);
}
