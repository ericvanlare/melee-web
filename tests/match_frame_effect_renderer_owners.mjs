// Renderer schema3 owner rows. Historical schema2 has a separate exact dispatch.
export function rendererOwnerObservationFailures(receipt) {
  const failures = [];
  if (!receipt || typeof receipt !== 'object' || Array.isArray(receipt)) return ['owner_receipt_shape'];
  const u32 = x => Number.isInteger(x) && x >= 0 && x <= 0xffffffff;
  const u64 = x => typeof x === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(x) && BigInt(x) <= 0xffffffffffffffffn;
  const exact = (row, keys) => row && typeof row === 'object' && !Array.isArray(row) &&
    Object.keys(row).length === keys.length && keys.every(k => Object.hasOwn(row,k));
  const renderer = receipt.renderer;
  const layouts = Array.isArray(receipt.layout_entries) ? receipt.layout_entries : [];
  const ownerRows = Array.isArray(receipt.provenance) ? receipt.provenance : [];
  const resourceRows = Array.isArray(receipt.resources) ? receipt.resources : [];
  const handleRows = Array.isArray(receipt.handles) ? receipt.handles : [];
  const bindings = Array.isArray(receipt.binding_entries) ? receipt.binding_entries : [];
  const rawResourceKeys=['capture_id','owner_id','kind','word_count','words'];
  const rawHandleKeys=['capture_id','kind','reserved'];
  const rawBindingKeys=['bind_group_id','resource_id','offset','size','binding','kind'];
  const validResourceRow=r => exact(r,rawResourceKeys) && u64(r.capture_id) && r.capture_id!=='0' &&
      u64(r.owner_id) && u32(r.kind) && [1,2,3,4,5,6,9].includes(r.kind) && u32(r.word_count) && r.word_count<=16 &&
      Array.isArray(r.words) && r.words.length===16 && r.words.every(u32);
  const validHandleRow=h => exact(h,rawHandleKeys) && u64(h.capture_id) && h.capture_id!=='0' &&
      u32(h.kind) && h.kind>=1 && h.kind<=9 && h.reserved===0;
  const validBindingRow=e => exact(e,rawBindingKeys) && u64(e.bind_group_id) && e.bind_group_id!=='0' &&
      u64(e.resource_id) && e.resource_id!=='0' && u64(e.offset) && u64(e.size) && u32(e.binding) && [1,3,4].includes(e.kind);
  if (!Array.isArray(receipt.resources) || resourceRows.some(r=>!validResourceRow(r)) ||
      !Array.isArray(receipt.handles) || handleRows.some(h=>!validHandleRow(h)) ||
      !Array.isArray(receipt.binding_entries) || bindings.some(e=>!validBindingRow(e)))
    failures.push('owner_raw_collection_rows');
  // Authored browser bounds: 2 static + 1 uniform + 2*GX_MAX_TEXMAP(8);
  // 4 shared + StagingBufferCount(4) + 3 layouts + 3 groups + empty texture/view/sampler.
  if (!Array.isArray(receipt.layout_entries) || receipt.layout_entries.length > 19 ||
      receipt.layout_entries.length !== renderer?.layout_entry_count || !u32(renderer?.layout_entry_count))
    failures.push('layout_entry_count');
  if (!Array.isArray(receipt.provenance) || receipt.provenance.length > 17 ||
      receipt.provenance.length !== renderer?.provenance_count || !u32(renderer?.provenance_count))
    failures.push('provenance_count');
  const resources = new Map(resourceRows.filter(validResourceRow).map(r => [r.capture_id,r]));
  const handles = new Map(handleRows.filter(validHandleRow).map(h => [h.capture_id,h.kind]));
  const indices = new Map();
  for (const row of layouts) {
    const parent = resources.get(row?.layout_id), next = indices.get(row?.layout_id) ?? 0;
    if (!exact(row,['layout_id','entry_index','words']) || !u64(row.layout_id) || parent?.kind !== 9 ||
        !u32(row.entry_index) || row.entry_index !== next || row.entry_index >= parent.words?.[0] ||
        !Array.isArray(row.words) || row.words.length !== 15 || !row.words.every(u32) || row.words[0] !== row.entry_index)
      failures.push('layout_entry_rows');
    indices.set(row?.layout_id,next+1);
  }
  for (const resource of resources.values()) if (resource?.kind === 9 &&
      (![1,2,16].includes(resource.words?.[0]) || (indices.get(resource.capture_id) ?? 0) !== resource.words?.[0]))
    failures.push('layout_descriptor_rows');
  const seen = new Set(), roles = new Set(), serials = new Set(), epochs = new Set(), devices = new Set();
  const validOwnerRows = [];
  const kindForRole = role => role <= 5 ? 3 : role <= 8 ? 9 : role <= 11 ? 5 : role === 12 ? 2 : role === 13 ? 1 : 4;
  for (const row of ownerRows) {
    const resource = resources.get(row?.capture_id), roleKey = `${row?.role}:${row?.slot}`;
    if (!exact(row,['capture_id','device_id','creation_serial','renderer_epoch','role','slot']) ||
        !u64(row.capture_id) || row.capture_id === '0' || !u64(row.device_id) || handles.get(row.device_id) !== 7 ||
        !u64(row.creation_serial) || row.creation_serial === '0' || !u64(row.renderer_epoch) || row.renderer_epoch === '0' ||
        !u32(row.role) || row.role < 1 || row.role > 14 || !u32(row.slot) || (row.role === 5 ? row.slot >= 4 : row.slot !== 0) ||
        !resource || resource.kind !== kindForRole(row.role) || seen.has(row.capture_id) || roles.has(roleKey) || serials.has(row.creation_serial) ||
        (resource.kind !== 1 && resource.owner_id !== row.device_id)) failures.push('owner_provenance_rows');
    else validOwnerRows.push(row);
    seen.add(row?.capture_id);roles.add(roleKey);serials.add(row?.creation_serial);epochs.add(row?.renderer_epoch);devices.add(row?.device_id);
  }
  if (epochs.size > 1 || devices.size > 1) failures.push('owner_epoch_or_device');
  // Only fully checked rows enter the lookup; malformed input is a refusal,
  // never an exception or a null-key provenance alias.
  const provenance = new Map(validOwnerRows.map(p => [p.capture_id,p]));
  for (const row of validOwnerRows) {
    const resource = resources.get(row.capture_id);
    if (resource?.kind === 1 && (!provenance.has(resource.owner_id) || resources.get(resource.owner_id)?.kind !== 2 ||
        provenance.get(resource.owner_id)?.device_id !== row.device_id || provenance.get(resource.owner_id)?.renderer_epoch !== row.renderer_epoch))
      failures.push('view_parent_provenance');
    if (resource?.kind === 5) {
      if (!Array.isArray(resource.words) || !resource.words.every(u32) || resource.words.length !== 16) {
        failures.push('group_owner_graph');continue;
      }
      const layoutId = String(BigInt(resource.words[0]) | BigInt(resource.words[1]) << 32n);
      const layout = resources.get(layoutId), expectedRole = row.role - 3;
      const entries = bindings.filter(validBindingRow).filter(e => e.bind_group_id === row.capture_id);
      if (layout?.kind !== 9 || provenance.get(layoutId)?.role !== expectedRole ||
          provenance.get(layoutId)?.device_id !== row.device_id || provenance.get(layoutId)?.renderer_epoch !== row.renderer_epoch ||
          entries.length !== resource.words[2] || entries.length !== layout?.words[0] ||
          entries.some((e,i) => e.binding !== i || !resources.has(e.resource_id) || !provenance.has(e.resource_id) ||
            provenance.get(e.resource_id)?.device_id !== row.device_id || provenance.get(e.resource_id)?.renderer_epoch !== row.renderer_epoch))
        failures.push('group_owner_graph');
    }
  }
  return failures;
}
