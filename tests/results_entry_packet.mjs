/** Entry-only local debug evidence; no writes or game calls other than getter. */
import assert from 'node:assert/strict';

export async function readResultsEntryPacket(page) {
  return page.evaluate(()=>{
    if(typeof Module==='undefined'||typeof Module._melee_web_native_menu_results_entry_packet!=='function')
      return {status:'getter-unavailable'};
    const packet=JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_results_entry_packet()));
    return {status:packet===null?'no-entry':'captured',packet};
  });
}

export function bindResultsEntryPacket(packet, servedArtifacts) {
  assert.equal(packet.schema,'melee-web-results-entry-v1');
  assert.equal(packet.abi.target,'wasm32');
  assert.equal(packet.abi.byte_order,'little-endian');
  assert.equal(packet.abi.pointer_bytes,4);
  assert(Number.isInteger(packet.match_index)&&packet.match_index>0&&packet.match_index<=0xffffffff);
  assert(Number.isInteger(packet.entry_seed)&&packet.entry_seed>=0&&packet.entry_seed<=0xffffffff);
  for(const [field,type] of [['terminal_hex','MatchExitInfo'],['results_info_hex','ResultsMatchInfo']]){
    assert(Number.isInteger(packet.sizeof[type])&&packet.sizeof[type]>0);
    assert.match(packet[field],/^[0-9a-f]+$/);
    assert.equal(packet[field].length,2*packet.sizeof[type]);
  }
  assert.equal(packet.pad.bytes,30+3*4*66);
  assert.match(packet.pad.hex,/^[0-9a-f]+$/);
  assert.equal(packet.pad.hex.length,packet.pad.bytes*2);
  const artifacts=['gameplay_menu_browser.js','gameplay_menu_browser.wasm'].map(name=>{
    const matches=servedArtifacts.filter(row=>new URL(row.url).pathname.endsWith('/'+name));
    assert(matches.length>0,`Missing served ${name} identity`);
    const first=matches[0];
    assert(first.status===200&&Number.isInteger(first.bytes)&&first.bytes>0);
    assert.match(first.sha256,/^[0-9a-f]{64}$/);
    assert(matches.every(row=>row.status===200&&row.sha256===first.sha256&&row.bytes===first.bytes),
      `Conflicting served ${name} identities`);
    return {...first};
  });
  return {scope:'Local ABI debug packet only; not a PPC image, exact replay or timing claim',
    build_binding:{status:'bound',kind:'observed-http-response-sha256',artifacts},packet};
}
