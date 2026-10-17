"""Actual native cache/TextureRef borrowing; synthetic GPU endpoints only."""
from pathlib import Path
import json
import shlex
import subprocess
import sys
from owned_test_workspace import OwnedWorkspaceTests

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from check_gameplay import node_runtime

def section(text,begin,end):
    if text.count(begin)!=1:
        raise AssertionError('Actual production seam missing/ambiguous: '+begin)
    start=text.index(begin)
    return text[start:text.index(end,start)]

def actual_control_source(root):
    """Extract current full bodies every run; stale fixture copies cannot pass."""
    aura=root/'.deps/aurora'
    texture=(aura/'lib/gx/texture.cpp').read_text()
    gx=(aura/'lib/gx/gx.cpp').read_text()
    bridges=(aura/'lib/gfx/texture.cpp').read_text()
    project=(root/'src/gameplay_menu_frame_effect.cpp').read_text()
    fixture=(root/'tests/selected_cache_owner_controls.cpp').read_text()
    expected='texture::observe_selected_cached_texture(i,tex,selectedFrameIndex,selectedFrameId);'
    if gx.count(expected)!=1:
        raise AssertionError('Actual GX first-sampled cache capture call changed')
    replacements={
        '// ACTUAL_TEXTURE_OWNER_BRIDGES': 'namespace aurora::gfx {\n'+section(bridges,'namespace detail {\nvoid bind_texture_ref_owner','\n#endif')+'\n}',
        '// ACTUAL_CACHE_ENTRY_AND_GLOBALS':section(texture,'struct DynamicPaletteKey {','\nstruct TextureContentKey')+'\n'+
            section(texture,'absl::flat_hash_map<u32, CachedTextureEntry> s_textureObjectCaches;','\nabsl::flat_hash_map<u32, TlutObjectCache>')+'\n'+
            section(texture,'absl::flat_hash_map<u32, TlutObjectCache> s_tlutObjectCaches;','\nabsl::flat_hash_map<TextureContentKey, ContentCacheEntry>')+'\n'+
            section(texture,'absl::flat_hash_map<uint64_t, absl::flat_hash_set<u32>> s_replacementUsers;','\nstd::list<TextureContentKey>')+'\n'+
            section(texture,'uint64_t s_frameCount = 0;','\nTextureStats s_stats;')+'\nTextureStats s_stats;',
        '// ACTUAL_PENDING_INVALIDATION_OPERATIONS':section(texture,'void do_clear_static_texture_cache() noexcept {','\nDynamicPaletteKey make_dynamic_palette_key'),
        '// ACTUAL_CACHE_STORE':section(texture,'void clear_texture_dependency(u32 texObjId, u32 tlutObjId, uint64_t replacementId) {','\nvoid touch_content_cache'),
        '// ACTUAL_CACHE_OPERATIONS':section(texture,'// Private exact authored-key borrow;','\n#endif'),
        '// ACTUAL_GENERATION_OPERATIONS':section(texture,'void invalidate_bindings() noexcept {','\n\nvoid invalidate_replacement')+'\n'+
            section(texture,'void clear_static_texture_cache() noexcept {','\n\nvoid evict_copy_texture'),
        '// ACTUAL_REPLACEMENT_INVALIDATION':section(texture,'void invalidate_replacement(uint64_t replacementId) noexcept {','\n\ngfx::TextureHandle resolve_static_texture'),
        '// ACTUAL_CACHE_RETIREMENT':section(texture,'void evict_texture_object(u32 texObjId) noexcept {','\nvoid evict_tlut_object'),
        '// ACTUAL_GX_OPERATIONS':section(gx,'void request_selected_sampled_texture() noexcept {','\n#endif'),
        '// ACTUAL_PROJECT_ISSUER':section(project,'int melee_web_native_menu_frame_effect_issue_selected_texture(){','\n#endif'),
    }
    for marker,body in replacements.items():
        if fixture.count(marker)!=1:
            raise AssertionError('Native fixture extraction marker changed: '+marker)
        fixture=fixture.replace(marker,body)
    return fixture

def control_link_inputs(root):
    # Existing validated private producer link order; real libraries, no stubs.
    build=root/'build/browser-first-draw-components-release'
    line=next((line for line in (build/'build.ninja').read_text().splitlines()
               if line.lstrip().startswith('LINK_LIBRARIES =') and 'libabsl_raw_hash_set.a' in line),None)
    if line is None:
        raise AssertionError('Actual private Abseil link row missing')
    archives=[(build/value).resolve() for value in shlex.split(line.split('=',1)[1])
              if value.endswith('/libfmt.a') or '/libabsl_' in value]
    if not any(p.name=='libfmt.a' for p in archives) or not any(p.name=='libabsl_raw_hash_set.a' for p in archives):
        raise AssertionError('Actual fmt/Abseil archive closure missing')
    for archive in archives:
        if not archive.is_file():
            raise AssertionError('Actual producer archive missing: '+str(archive))
    return archives

class SelectedCacheOwnerTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work=cls.new_workspace(ROOT,'selected-cache-owner-')

    def test_actual_first_sample_cache_owner_issue_current_and_lifetime(self):
        database=ROOT/'build/browser-first-draw-components-release/compile_commands.json'
        compiler=ROOT/'.deps/emsdk/upstream/emscripten/em++'
        if not database.is_file() or not compiler.is_file():
            self.skipTest('Configure private first-draw profile with pinned Emscripten for actual owner control')
        rows=json.loads(database.read_text())
        source=ROOT/'.deps/aurora/lib/gx/texture.cpp'
        row=next((r for r in rows if Path(r['file']).resolve()==source.resolve()),None)
        self.assertIsNotNone(row,'Actual GX texture compile row missing')
        args=shlex.split(row['command'])[1:];retained=[];i=0
        while i<len(args):
            arg=args[i]
            if arg in ('-o','-MT','-MF','-MQ'):
                i+=2;continue
            if arg in ('-c','-MD','-MMD',row['file']):
                i+=1;continue
            retained.append(arg);i+=1
        control=self.work/'actual-control.cpp';control.write_text(actual_control_source(ROOT))
        output=self.work/'actual-control.js'
        archives=control_link_inputs(ROOT)
        (self.work/'link-inputs.json').write_text(json.dumps(list(map(str,archives)),indent=2)+'\n')
        command=[str(compiler),*retained,'-UNDEBUG','-I',str(ROOT/'.deps/aurora'),str(control),*map(str,archives),
                 '-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-sASSERTIONS=1','-o',str(output)]
        built=subprocess.run(command,cwd=row['directory'],capture_output=True,text=True,timeout=90)
        (self.work/'compile.log').write_text(built.stdout+built.stderr)
        self.assertEqual(built.returncode,0,built.stdout+built.stderr)
        for mode in range(49):
            result=subprocess.run([str(node_runtime(ROOT)),str(output),str(mode)],cwd=ROOT,
                                  capture_output=True,text=True,timeout=30)
            (self.work/f'case-{mode}.log').write_text(result.stdout+result.stderr)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            expected=f'actual cache-owner case {mode} passed' if mode<28 or mode>=32 else f'actual project issue case {mode} error='
            self.assertIn(expected,result.stdout)
