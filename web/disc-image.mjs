/**
 * Bounded browser access to a local Melee GALE01 revision 2 ISO/GCM/CISO.
 *
 * Public API:
 *   const disc = await openDiscImage(fileOrBlob);
 *   const bytes = await disc.read(offset, size); // Uint8Array
 *   const entries = await disc.files();           // Map<string, DiscFile>
 *   const bytes = await disc.readFile("PlMr.dat");
 *   const font = await extractFontAtlas(disc);    // Uint8Array
 *
 * `fileOrBlob` is kept local by the caller.  Reads are asynchronous and use
 * Blob#slice, with no operation reading more than MAX_READ_SIZE at a time.
 * CISO's sparse blocks are represented as zero-filled ranges and are never
 * materialized as a complete image.
 */

export const CISO_HEADER_SIZE = 0x8000;
export const MAX_FST_SIZE = 16 * 1024 * 1024;
export const MAX_READ_SIZE = 1024 * 1024;
/** Maximum output allocated by one logical read (callers can stream slices). */
export const MAX_LOGICAL_READ_SIZE = 64 * 1024 * 1024;
export const FONT_START = 0x8040cd40;
export const FONT_END = 0x80430b40;
export const FONT_SIZE = FONT_END - FONT_START;

const DISC_MAGIC = 0xc2339f3d;
const textDecoder = new TextDecoder("ascii", { fatal: true });

/** Error raised when a Blob is not a safe supported Melee disc image. */
export class DiscFormatError extends Error {
  constructor(message, options = undefined) {
    super(message, options);
    this.name = "DiscFormatError";
  }
}

/** A canonical file entry returned by DiscImage#files. */
export class DiscFile {
  constructor(path, offset, size) {
    this.path = path;
    this.offset = offset;
    this.size = size;
    Object.freeze(this);
  }
}

/** Reject ambiguous paths rather than normalizing them. */
export function relativeDiscPath(value) {
  if (typeof value !== "string") {
    throw new DiscFormatError("disc path must be a canonical relative path");
  }
  const parts = value.split("/");
  if (!value || parts.some(part => part === "" || part === "." || part === "..")) {
    throw new DiscFormatError("disc path must be a canonical relative path");
  }
  if (value.includes("\\") || value.includes(":") ||
      [...value].some(character => character.codePointAt(0) < 0x20)) {
    throw new DiscFormatError("disc path contains an unsupported character");
  }
  return value;
}

function isBlobLike(value) {
  return value !== null && typeof value === "object" &&
    Number.isSafeInteger(value.size) && value.size >= 0 &&
    typeof value.slice === "function" && typeof value.arrayBuffer === "function";
}

function bytesFromBuffer(buffer) {
  if (buffer instanceof ArrayBuffer) return new Uint8Array(buffer);
  if (ArrayBuffer.isView(buffer)) {
    return new Uint8Array(buffer.buffer, buffer.byteOffset, buffer.byteLength);
  }
  throw new TypeError("Blob#arrayBuffer() did not return an ArrayBuffer");
}

function asBytes(value, expectedSize) {
  const bytes = bytesFromBuffer(value);
  if (bytes.byteLength !== expectedSize) {
    throw new DiscFormatError("truncated disc data");
  }
  return bytes;
}

function readU32(bytes, offset, littleEndian = false) {
  if (offset < 0 || offset + 4 > bytes.byteLength) {
    throw new DiscFormatError("truncated disc data");
  }
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
    .getUint32(offset, littleEndian);
}

function describe(error) {
  return error && typeof error.message === "string" ? error.message : String(error);
}

/**
 * An opened image.  Constructing one starts validation; callers normally use
 * `await DiscImage.open(blob)` or `await openDiscImage(blob)`.
 */
export class DiscImage {
  constructor(source) {
    if (!isBlobLike(source)) {
      throw new TypeError("DiscImage requires a browser Blob or File");
    }
    this.source = source;
    this.physicalSize = source.size;
    this.logicalSize = source.size;
    this.blockSize = 0;
    this._mapping = null;
    this._fstOffset = 0;
    this._fstSize = 0;
    this._filesPromise = null;
    this._ready = this._initialize();
  }

  static async open(source) {
    const image = new DiscImage(source);
    await image.ready();
    return image;
  }

  /** Resolve after the bounded identity/CISO checks have completed. */
  async ready() {
    await this._ready;
    return this;
  }

  async _initialize() {
    let magic;
    try {
      magic = await this._readPhysical(0, 4);
    } catch (error) {
      if (error instanceof DiscFormatError) throw error;
      throw new DiscFormatError(`could not read disc header: ${describe(error)}`, { cause: error });
    }
    if (magic.length === 4 && magic[0] === 0x43 && magic[1] === 0x49 &&
        magic[2] === 0x53 && magic[3] === 0x4f) {
      await this._readCisoHeader();
    }

    let header;
    try {
      header = await this._readLogical(0, 0x440);
    } catch (error) {
      if (error instanceof DiscFormatError) throw error;
      throw new DiscFormatError(`could not read disc header: ${describe(error)}`, { cause: error });
    }
    const identity = new TextDecoder("latin1").decode(header.subarray(0, 8));
    if (identity !== "GALE01\x00\x02") {
      throw new DiscFormatError(
        "expected Super Smash Bros. Melee USA revision 2 (GALE01 1.02)",
      );
    }
    if (readU32(header, 0x1c) !== DISC_MAGIC) {
      throw new DiscFormatError("invalid GameCube disc magic");
    }
    this._fstOffset = readU32(header, 0x424);
    this._fstSize = readU32(header, 0x428);
  }

  async _readCisoHeader() {
    if (this.physicalSize < CISO_HEADER_SIZE) {
      throw new DiscFormatError("truncated CISO header");
    }
    const header = await this._readPhysical(0, CISO_HEADER_SIZE);
    const blockSize = readU32(header, 4, true);
    if (blockSize < 0x8000 || blockSize > 32 * 1024 * 1024 ||
        (blockSize & (blockSize - 1)) !== 0) {
      throw new DiscFormatError("unsupported CISO block size");
    }
    const mapping = new Array(CISO_HEADER_SIZE - 8);
    let presentCount = 0;
    for (let index = 0; index < mapping.length; ++index) {
      const present = header[8 + index];
      if (present !== 0 && present !== 1) {
        throw new DiscFormatError("invalid CISO block map");
      }
      mapping[index] = present ? presentCount++ : null;
    }
    const mappedBytes = CISO_HEADER_SIZE + presentCount * blockSize;
    if (mappedBytes > this.physicalSize) {
      throw new DiscFormatError("truncated CISO data blocks");
    }
    this._mapping = mapping;
    this.blockSize = blockSize;
    this.logicalSize = mapping.length * blockSize;
  }

  _checkRange(offset, size, limit = this.logicalSize) {
    if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(size) ||
        offset < 0 || size < 0 || offset > limit || size > limit - offset) {
      throw new DiscFormatError("disc byte range is outside the image");
    }
  }

  async _readPhysical(offset, size) {
    this._checkRange(offset, size, this.physicalSize);
    const output = new Uint8Array(size);
    let written = 0;
    while (written < size) {
      const partSize = Math.min(MAX_READ_SIZE, size - written);
      const slice = this.source.slice(offset + written, offset + written + partSize);
      let buffer;
      try {
        buffer = await slice.arrayBuffer();
      } catch (error) {
        throw new DiscFormatError(`disc data read failed: ${describe(error)}`, { cause: error });
      }
      const part = asBytes(buffer, partSize);
      output.set(part, written);
      written += partSize;
    }
    return output;
  }

  async _readLogical(offset, size) {
    this._checkRange(offset, size);
    if (size > MAX_LOGICAL_READ_SIZE) {
      throw new DiscFormatError("logical read exceeds the 64 MiB limit");
    }
    const output = new Uint8Array(size);
    let written = 0;
    while (written < size) {
      if (this._mapping === null) {
        const partSize = Math.min(MAX_READ_SIZE, size - written);
        output.set(await this._readPhysical(offset + written, partSize), written);
        written += partSize;
        continue;
      }
      const logicalOffset = offset + written;
      const block = Math.floor(logicalOffset / this.blockSize);
      const within = logicalOffset - block * this.blockSize;
      const partSize = Math.min(MAX_READ_SIZE, size - written, this.blockSize - within);
      const mapped = this._mapping[block];
      if (mapped !== null) {
        const physicalOffset = CISO_HEADER_SIZE + mapped * this.blockSize + within;
        output.set(await this._readPhysical(physicalOffset, partSize), written);
      }
      // A missing CISO block is already zero initialized in output.
      written += partSize;
    }
    return output;
  }

  /** Read an exact logical disc range as a Uint8Array. */
  async read(offset, size) {
    await this._ready;
    return this._readLogical(offset, size);
  }

  /**
   * Parse the GameCube FST into a Map keyed by canonical relative path.
   * The validated parse is cached privately; each call returns a fresh Map.
   */
  async files() {
    await this._ready;
    if (this._filesPromise === null) this._filesPromise = this._parseFiles();
    // Keep the validated parse private: callers may add/delete Map entries
    // while inspecting a result without changing future reads.
    return new Map(await this._filesPromise);
  }

  /** Read a complete file, or a bounded file-relative slice, by exact path. */
  async readFile(path, offset = 0, size = undefined) {
    relativeDiscPath(path);
    const entry = (await this.files()).get(path);
    if (!entry) throw new DiscFormatError(`disc file was not found: ${path}`);
    if (!Number.isSafeInteger(offset) || offset < 0 || offset > entry.size) {
      throw new DiscFormatError("selected byte range is outside the disc file");
    }
    const length = size === undefined ? entry.size - offset : size;
    if (!Number.isSafeInteger(length) || length < 0 || length > entry.size - offset) {
      throw new DiscFormatError("selected byte range is outside the disc file");
    }
    return this.read(entry.offset + offset, length);
  }

  async _parseFiles() {
    if (this._fstOffset < 0x440 || this._fstSize < 12 || this._fstSize > MAX_FST_SIZE) {
      throw new DiscFormatError("invalid or oversized filesystem table");
    }
    const fst = await this._readLogical(this._fstOffset, this._fstSize);
    if (fst.length < 12) throw new DiscFormatError("invalid filesystem root or entry count");
    const rootTag = readU32(fst, 0);
    const rootParent = readU32(fst, 4);
    const count = readU32(fst, 8);
    if (rootTag !== 0x01000000 || rootParent !== 0 || count < 1 || count > Math.floor(fst.length / 12)) {
      throw new DiscFormatError("invalid filesystem root or entry count");
    }
    const namesStart = count * 12;
    if (namesStart >= fst.length) throw new DiscFormatError("invalid filesystem string table");
    const names = fst.subarray(namesStart);

    const nameAt = offset => {
      if (!Number.isSafeInteger(offset) || offset >= names.length) {
        throw new DiscFormatError("filesystem name offset is out of bounds");
      }
      let end = offset;
      while (end < names.length && names[end] !== 0) ++end;
      if (end >= names.length) throw new DiscFormatError("unterminated filesystem name");
      for (let index = offset; index < end; ++index) {
        if (names[index] > 0x7f) throw new DiscFormatError("non-ASCII filesystem name");
      }
      let name;
      try {
        name = textDecoder.decode(names.subarray(offset, end));
      } catch (error) {
        throw new DiscFormatError("non-ASCII filesystem name", { cause: error });
      }
      relativeDiscPath(name);
      if (name.includes("/")) {
        throw new DiscFormatError("filesystem name contains a path separator");
      }
      return name;
    };

    const stack = [{ end: count, parent: 0, path: "" }];
    const result = new Map();
    const seen = new Set();
    for (let index = 1; index < count; ++index) {
      while (stack.length > 1 && index >= stack[stack.length - 1].end) stack.pop();
      const frame = stack[stack.length - 1];
      if (!frame || index >= frame.end) {
        throw new DiscFormatError("invalid filesystem directory hierarchy");
      }
      const entryOffset = index * 12;
      const tag = readU32(fst, entryOffset);
      const first = readU32(fst, entryOffset + 4);
      const second = readU32(fst, entryOffset + 8);
      const kind = tag >>> 24;
      const nameOffset = tag & 0x00ffffff;
      if (kind !== 0 && kind !== 1) throw new DiscFormatError("unknown filesystem entry type");
      const name = nameAt(nameOffset);
      const path = frame.path ? `${frame.path}/${name}` : name;
      if (seen.has(path)) throw new DiscFormatError("duplicate filesystem path");
      seen.add(path);
      if (kind === 1) {
        if (first !== frame.parent || index >= second || second > frame.end) {
          throw new DiscFormatError("invalid filesystem directory hierarchy");
        }
        stack.push({ end: second, parent: index, path });
      } else {
        this._checkRange(first, second);
        const entry = new DiscFile(path, first, second);
        result.set(path, entry);
      }
    }
    return result;
  }

  /** Read the pinned original GALE01 font atlas from the DOL. */
  async extractFontAtlas() {
    return extractFontAtlas(this);
  }
}

/** Open a local Blob/File and validate its GALE01 revision and container. */
export async function openDiscImage(source) {
  return DiscImage.open(source);
}

function bytesForFontHeader(header) {
  if (header instanceof Uint8Array) return header;
  if (header instanceof ArrayBuffer) return new Uint8Array(header);
  if (ArrayBuffer.isView(header)) return new Uint8Array(header.buffer, header.byteOffset, header.byteLength);
  throw new TypeError("fontFileRange requires a byte buffer");
}

/** Locate the fixed original font range within a 0x100-byte DOL header. */
export function fontFileRange(headerValue) {
  const header = bytesForFontHeader(headerValue);
  if (header.byteLength !== 0x100) throw new DiscFormatError("truncated DOL header");
  const matches = [];
  for (let index = 0; index < 18; ++index) {
    const offset = readU32(header, index * 4);
    const address = readU32(header, 0x48 + index * 4);
    const size = readU32(header, 0x90 + index * 4);
    const end = address + size;
    if (size && address <= FONT_START && FONT_END <= end) {
      if (index < 7 || offset < 0x100 || end > 0x100000000 || offset + size > 0x100000000) {
        throw new DiscFormatError("font atlas is not inside a valid DOL data section");
      }
      matches.push({ offset: offset + FONT_START - address, size: FONT_SIZE });
    }
  }
  if (matches.length !== 1) {
    throw new DiscFormatError("font atlas must resolve to exactly one complete DOL data section");
  }
  return matches[0];
}

/** Extract FONT_START..FONT_END from an opened DiscImage (or open a Blob). */
export async function extractFontAtlas(image) {
  const disc = image instanceof DiscImage ? image : await openDiscImage(image);
  await disc.ready();
  const dolBytes = await disc.read(0x420, 4);
  const dol = readU32(dolBytes, 0);
  if (dol < 0x440) throw new DiscFormatError("invalid DOL file offset");
  const range = fontFileRange(await disc.read(dol, 0x100));
  return disc.read(dol + range.offset, range.size);
}
