import assert from "node:assert/strict";
import { Blob } from "node:buffer";
import {
  CISO_HEADER_SIZE,
  DiscFormatError,
  FONT_END,
  FONT_SIZE,
  FONT_START,
  MAX_READ_SIZE,
  extractFontAtlas,
  fontFileRange,
  openDiscImage,
} from "../web/disc-image.mjs";

const BLOCK = 0x8000;
const FST_OFFSET = 0x800;
const PAYLOAD_OFFSET = 2 * BLOCK + 32;
const PAYLOAD = new TextEncoder().encode("synthetic browser disc payload");

function put32(bytes, offset, value, littleEndian = false) {
  new DataView(bytes.buffer).setUint32(offset, value, littleEndian);
}

function filesystemTable({ duplicate = false, badHierarchy = false } = {}) {
  const fileName = duplicate ? "target.dat\0target.dat\0" : "models\0target.dat\0";
  const table = new Uint8Array(
    36 + new TextEncoder().encode(fileName).length,
  );
  put32(table, 0, 0x01000000);
  put32(table, 8, 3);
  put32(table, 12, 0x01000000);
  if (badHierarchy) put32(table, 16, 2);
  put32(table, 20, badHierarchy ? 2 : 3);
  put32(table, 24, duplicate ? 0 : 7);
  put32(table, 28, PAYLOAD_OFFSET);
  put32(table, 32, PAYLOAD.length);
  table.set(new TextEncoder().encode(fileName), 36);
  return table;
}

function rawImage(options = {}) {
  const fst = filesystemTable(options);
  const image = new Uint8Array(Math.max(3 * BLOCK, PAYLOAD_OFFSET + PAYLOAD.length));
  image.set(new TextEncoder().encode("GALE01\0\x02"), 0);
  put32(image, 0x1c, 0xc2339f3d);
  put32(image, 0x424, FST_OFFSET);
  put32(image, 0x428, fst.length);
  image.set(fst, FST_OFFSET);
  image.set(PAYLOAD, PAYLOAD_OFFSET);
  return image;
}

function sparseCiso(image) {
  const header = new Uint8Array(CISO_HEADER_SIZE);
  header.set(new TextEncoder().encode("CISO"), 0);
  put32(header, 4, BLOCK, true);
  const chunks = [];
  for (let index = 0; index < image.length / BLOCK; ++index) {
    const block = image.subarray(index * BLOCK, (index + 1) * BLOCK);
    if (block.some(value => value !== 0)) {
      header[8 + index] = 1;
      chunks.push(block);
    }
  }
  const result = new Uint8Array(CISO_HEADER_SIZE + chunks.length * BLOCK);
  result.set(header);
  let offset = CISO_HEADER_SIZE;
  for (const chunk of chunks) {
    result.set(chunk, offset);
    offset += BLOCK;
  }
  return result;
}

class TrackingBlob {
  constructor(bytes, calls) {
    this._blob = new Blob([bytes]);
    this.size = this._blob.size;
    this.calls = calls;
  }

  slice(start, end) {
    this.calls.push([start, end]);
    return this._blob.slice(start, end);
  }

  arrayBuffer() {
    throw new Error("the reader must use bounded slices instead of the whole Blob");
  }
}

function expectFormat(promise, pattern) {
  return assert.rejects(promise, error => {
    assert(error instanceof DiscFormatError);
    assert.match(error.message, pattern);
    return true;
  });
}

async function main() {
  const rawCalls = [];
  const raw = await openDiscImage(new TrackingBlob(rawImage(), rawCalls));
  assert.deepEqual(await raw.read(PAYLOAD_OFFSET, PAYLOAD.length), PAYLOAD);
  assert.deepEqual(await raw.readFile("models/target.dat"), PAYLOAD);
  const files = await raw.files();
  assert.equal(files.get("models/target.dat").offset, PAYLOAD_OFFSET);
  assert.equal(files.get("models/target.dat").size, PAYLOAD.length);
  assert(rawCalls.every(([start, end]) => end - start <= MAX_READ_SIZE));
  assert(rawCalls.every(([start, end]) => end <= raw.physicalSize));

  const ciso = await openDiscImage(new Blob([sparseCiso(rawImage())]));
  assert.deepEqual(await ciso.read(BLOCK - 2, BLOCK + 4), new Uint8Array(BLOCK + 4));
  assert.deepEqual(await ciso.readFile("models/target.dat", 4, 9), PAYLOAD.subarray(4, 13));
  assert.equal(ciso.logicalSize, (CISO_HEADER_SIZE - 8) * BLOCK);
  await expectFormat(ciso.read(0, 64 * 1024 * 1024 + 1), /64 MiB/);

  const fontImage = rawImage();
  const dol = 0x1000;
  const sectionOffset = 0x2000;
  const sectionAddress = FONT_START - 0x100;
  const sectionSize = FONT_SIZE + 0x200;
  const fontEnd = dol + sectionOffset + 0x100 + FONT_SIZE;
  const expanded = new Uint8Array(Math.max(fontImage.length, fontEnd));
  expanded.set(fontImage);
  put32(expanded, 0x420, dol);
  put32(expanded, dol + 7 * 4, sectionOffset);
  put32(expanded, dol + 0x48 + 7 * 4, sectionAddress);
  put32(expanded, dol + 0x90 + 7 * 4, sectionSize);
  const font = Uint8Array.from({ length: FONT_SIZE }, (_, index) => index & 0xff);
  expanded.set(font, dol + sectionOffset + 0x100);
  const fontDisc = await openDiscImage(new Blob([expanded]));
  assert.deepEqual(await extractFontAtlas(fontDisc), font);
  assert.deepEqual(fontFileRange(new Uint8Array(expanded.buffer, dol, 0x100)), {
    offset: sectionOffset + 0x100,
    size: FONT_SIZE,
  });
  assert.equal(FONT_END - FONT_START, FONT_SIZE);

  await expectFormat(openDiscImage(new Blob([new Uint8Array(10)])), /outside|truncated/);
  const badIdentity = rawImage();
  badIdentity[7] = 1;
  await expectFormat(openDiscImage(new Blob([badIdentity])), /revision 2/);
  await expectFormat(openDiscImage(new Blob([sparseCiso(rawImage()).subarray(0, 100)])), /CISO header/);
  const invalidMap = sparseCiso(rawImage());
  invalidMap[9] = 2;
  await expectFormat(openDiscImage(new Blob([invalidMap])), /block map/);
  const truncatedBlocks = sparseCiso(rawImage()).subarray(0, CISO_HEADER_SIZE + BLOCK - 1);
  await expectFormat(openDiscImage(new Blob([truncatedBlocks])), /data blocks/);
  await expectFormat((await openDiscImage(new Blob([rawImage({ badHierarchy: true })]))).files(), /hierarchy/);

  const duplicate = new Uint8Array(48 + 18);
  put32(duplicate, 0, 0x01000000);
  put32(duplicate, 8, 4);
  put32(duplicate, 12, 0x01000000);
  put32(duplicate, 20, 4);
  for (const entry of [24, 36]) {
    put32(duplicate, entry, 7);
    put32(duplicate, entry + 4, PAYLOAD_OFFSET);
    put32(duplicate, entry + 8, PAYLOAD.length);
  }
  duplicate.set(new TextEncoder().encode("models\0target.dat\0"), 48);
  const duplicateImage = rawImage();
  put32(duplicateImage, 0x428, duplicate.length);
  duplicateImage.set(duplicate, FST_OFFSET);
  await expectFormat((await openDiscImage(new Blob([duplicateImage]))).files(), /duplicate|path/);

  await expectFormat((await openDiscImage(new Blob([rawImage()]))).readFile("missing.dat"), /not found/);
  console.log("Browser ISO/CISO bounded reads, GALE01/FST validation and font extraction passed");
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
