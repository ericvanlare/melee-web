/** Bounded SHA-256 fingerprints for bytes submitted to Aurora's staging ring. */

export const STAGING_RING_BYTE_HASH_FRAME_CAP = 600;

export async function installStagingRingByteCapture(page, frameCap = STAGING_RING_BYTE_HASH_FRAME_CAP) {
  if (frameCap !== STAGING_RING_BYTE_HASH_FRAME_CAP) {
    throw new Error(`Staging byte capture is fixed at ${STAGING_RING_BYTE_HASH_FRAME_CAP} frames`);
  }
  return page.evaluate(cap => {
    if (window.__meleeWebStagingByteCapture) {
      throw new Error('Staging byte capture is already installed');
    }
    const roles = ['vertex', 'uniform', 'index', 'storage', 'texture-upload'];
    const capture = {
      schema: 'melee-web-staging-byte-hashes-v1',
      cap,
      active: false,
      status: 'installed',
      frames: [],
      errors: [],
      overflow: false,
      expected_cursor: 0,
      current_source_tag: null,
      rejected_source_tag: null,
      started_at_ms: null,
      completed_at_ms: null,
      fail(reason) {
        if (capture.status === 'incomplete' || capture.status === 'complete') return;
        capture.errors.push(reason);
        capture.status = 'incomplete';
        capture.active = false;
        capture.current_source_tag = null;
      },
      arm() {
        if (capture.status !== 'installed' || capture.active || capture.frames.length !== 0) {
          throw new Error('Staging byte capture can only be armed once before replay starts');
        }
        capture.active = true;
        capture.status = 'capturing';
        capture.started_at_ms = performance.now();
        return {status: capture.status, cap: capture.cap, start_cursor: 0, source_frame: 1};
      },
      setSourceDrawTag(sourceFrame, sourceCursor) {
        if (!capture.active) return false;
        if (capture.current_source_tag !== null) {
          capture.fail('nested_source_draw_tag');
          return false;
        }
        if (!Number.isInteger(sourceCursor) || !Number.isInteger(sourceFrame) ||
            sourceCursor !== capture.expected_cursor || sourceFrame !== sourceCursor + 1) {
          capture.rejected_source_tag = {source_frame: sourceFrame, source_cursor: sourceCursor,
            expected_cursor: capture.expected_cursor};
          capture.fail(capture.expected_cursor === 0 ? 'first_source_cursor_or_frame_mismatch' :
            'missing_duplicate_or_reordered_source_cursor');
          return false;
        }
        capture.current_source_tag = {source_cursor: sourceCursor, source_frame: sourceFrame,
          gameplay_ordinal: sourceCursor + 1, source_draw_ordinal: sourceCursor + 1};
        return true;
      },
      clearSourceDrawTag() {
        capture.current_source_tag = null;
      },
      recordFrame(frame) {
        if (!capture.active || capture.current_source_tag === null) return;
        const tag = capture.current_source_tag;
        capture.current_source_tag = null;
        if (!frame || !Number.isInteger(frame.frame_id) || !Number.isInteger(frame.frame_index) ||
            !Number.isInteger(frame.staging_slot) || !Array.isArray(frame.writes) || frame.writes.length > 5) {
          capture.fail('invalid_frame_record');
          return;
        }
        if (frame.source_cursor !== tag.source_cursor || frame.source_frame !== tag.source_frame) {
          capture.fail('source_tag_frame_mismatch');
          return;
        }
        if (tag.source_cursor !== capture.expected_cursor || tag.source_frame !== tag.source_cursor + 1) {
          capture.fail('missing_duplicate_or_reordered_source_cursor');
          return;
        }
        const seen = new Set();
        for (const write of frame.writes) {
          if (!Number.isInteger(write.destination_role) || write.destination_role < 0 ||
              write.destination_role >= roles.length || seen.has(write.destination_role) ||
              !Number.isInteger(write.destination_offset) || write.destination_offset < 0 ||
              !Number.isInteger(write.byte_length) || write.byte_length <= 0 ||
              write.byte_length % 4 !== 0 || !/^[0-9a-f]{64}$/.test(write.sha256)) {
            capture.fail('invalid_write_fingerprint');
            return;
          }
          seen.add(write.destination_role);
          write.destination_role_name = roles[write.destination_role];
        }
        capture.frames.push({
          gameplay_ordinal: tag.gameplay_ordinal,
          source_frame: tag.source_frame,
          source_cursor: tag.source_cursor,
          source_draw_ordinal: tag.source_draw_ordinal,
          renderer_frame_id: frame.frame_id,
          renderer_frame_index: frame.frame_index,
          staging_slot: frame.staging_slot,
          writes: frame.writes,
        });
        capture.expected_cursor++;
        if (capture.expected_cursor === cap) {
          capture.active = false;
          capture.status = 'complete';
          capture.completed_at_ms = performance.now();
        }
      },
      snapshot() {
        return {
          schema: capture.schema,
          cap: capture.cap,
          status: capture.status,
          active: capture.active,
          frame_count: capture.frames.length,
          pending_frame_count: 0,
          overflow: capture.overflow,
          errors: capture.errors.slice(),
          expected_cursor: capture.expected_cursor,
          current_source_tag: capture.current_source_tag,
          rejected_source_tag: capture.rejected_source_tag,
          started_at_ms: capture.started_at_ms,
          completed_at_ms: capture.completed_at_ms,
          frames: capture.frames.slice(),
        };
      },
    };
    window.__meleeWebStagingByteCapture = capture;
    return {status: capture.status, cap, source_draw_tag_supported: true};
  }, frameCap);
}

export async function armStagingRingByteCapture(page) {
  return page.evaluate(() => window.__meleeWebStagingByteCapture?.arm());
}

export async function readStagingRingByteCapture(page) {
  return page.evaluate(() => window.__meleeWebStagingByteCapture?.snapshot() ??
    {schema: 'melee-web-staging-byte-hashes-v1', status: 'not-installed'});
}

export async function readStagingRingByteCaptureStatus(page) {
  return page.evaluate(() => {
    const capture = window.__meleeWebStagingByteCapture;
    if (!capture) return {status: 'not-installed'};
    return {status: capture.status, active: capture.active, frame_count: capture.frames.length,
      expected_cursor: capture.expected_cursor, overflow: capture.overflow, errors: capture.errors.slice()};
  });
}

function validateCapture(label, capture, expectedSlots) {
  const required = STAGING_RING_BYTE_HASH_FRAME_CAP;
  if (capture?.schema !== 'melee-web-staging-byte-hashes-v1' || capture.status !== 'complete' ||
      capture.frame_count !== required || capture.frames?.length !== required ||
      capture.pending_frame_count !== 0 || capture.expected_cursor !== required ||
      capture.overflow || capture.errors?.length) {
    throw new Error(`${label} capture is incomplete or exceeded its bound`);
  }
  if (capture.requested_slots !== expectedSlots || capture.ring_status?.frame_slots !== expectedSlots ||
      capture.ring_status?.staging_buffers !== expectedSlots ||
      capture.ring_status?.selection?.byte_hash_enabled !== true) {
    throw new Error(`${label} ring identity does not match the requested ${expectedSlots}-slot byte-hash capture`);
  }
  for (let index = 0; index < required; index++) {
    const row = capture.frames[index];
    if (!row || row.gameplay_ordinal !== index + 1 || row.source_cursor !== index ||
        row.source_frame !== index + 1 || row.source_draw_ordinal !== index + 1 ||
        !Array.isArray(row.writes) || row.writes.length > 5) {
      throw new Error(`${label} capture has a missing, duplicate, or reordered source cursor at gameplay ordinal ${index + 1}`);
    }
  }
}

export function compareStagingRingByteCaptures(twoSlot, fourSlot) {
  const required = STAGING_RING_BYTE_HASH_FRAME_CAP;
  validateCapture('ring2', twoSlot, 2);
  validateCapture('ring4', fourSlot, 4);
  const normalize = row => ({
    gameplay_ordinal: row.gameplay_ordinal,
    source_cursor: row.source_cursor,
    source_frame: row.source_frame,
    source_draw_ordinal: row.source_draw_ordinal,
    writes: row.writes.map(({destination_role, destination_role_name, destination_offset,
      byte_length, sha256}) => ({destination_role, destination_role_name,
      destination_offset, byte_length, sha256})),
  });
  for (let index = 0; index < required; index++) {
    if (JSON.stringify(normalize(twoSlot.frames[index])) !== JSON.stringify(normalize(fourSlot.frames[index]))) {
      throw new Error(`submitted-byte SHA-256 or source-cursor mismatch at gameplay ordinal ${index + 1}`);
    }
  }
  return {
    status: 'equal',
    comparison: 'SHA-256 plus exact byte length/role/offset for every submitted Queue.WriteBuffer range',
    compared_identity: 'gameplay ordinal, MWRCv4 input cursor, source frame, and exact submitted ranges; renderer-global IDs and staging-slot IDs are retained as provenance only',
    normalized_field: 'staging_slot only',
    frames: required,
    fingerprinted_write_count: twoSlot.frames.reduce((sum, frame) => sum + frame.writes.length, 0),
  };
}
