"""Actual registry/slot/hook bodies over explicitly modeled source ABI leaves.
No DAT decode, real Ground tree, allocator, scheduler or transformation runs.
"""
from pathlib import Path
import os
import json
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('MELEE_WEB_CANDIDATE_SOURCE_ROOT',ROOT))
REFERENCE=Path(os.environ.get('MELEE_WEB_REFERENCE_SOURCE_ROOT',ROOT))

def body(source,signature):
    start=source.index(signature);opening=source.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

class StadiumSourceSlotOwnershipTests(unittest.TestCase):
    def reconstructed_adapters(self):
        """Use pinned upstream plus authoritative patch, never generated builds."""
        git=shutil.which('git')
        upstream=REFERENCE/'.deps/melee'
        paths=('src/melee/gr/grdatfiles.c','src/melee/gr/granime.c',
               'src/melee/lb/lbarchive.c')
        if not git or not (upstream/'.git').exists() or any(not (upstream/p).is_file() for p in paths):
            self.skipTest('Pinned Melee source dependency and git required')
        lock=json.loads((REFERENCE/'dependencies.lock.json').read_text())
        commit=lock['repositories']['melee']['commit']
        actual=subprocess.run([git,'rev-parse','HEAD'],cwd=upstream,
                              capture_output=True,text=True,timeout=10)
        self.assertEqual(actual.returncode,0,actual.stderr)
        self.assertEqual(actual.stdout.strip(),commit,'Melee source must match dependencies.lock.json')
        preview=Path(tempfile.mkdtemp(prefix='stadium-source-slot-adapters-'))
        print(f'Source-slot adapter reconstruction: {preview}',flush=True)
        for relative in paths:
            original=subprocess.run([git,'show',commit+':'+relative],cwd=upstream,
                                    capture_output=True,timeout=10)
            self.assertEqual(original.returncode,0,original.stderr.decode(errors='replace'))
            self.assertEqual((upstream/relative).read_bytes(),original.stdout,
                             'Pinned source file was modified: '+relative)
            target=preview/relative;target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(original.stdout)
        # Actual git patch application is the source authority. A missing hook
        # or stale/faulty patch fails; it is never treated as an optional skip.
        command=[git,'apply','--no-index',*['--include='+p for p in paths],
                 str(SOURCE/'patches/melee-gameplay.patch')]
        result=subprocess.run(command,cwd=preview,capture_output=True,text=True,timeout=30)
        (preview/'apply.stdout').write_text(result.stdout)
        (preview/'apply.stderr').write_text(result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return preview

    def test_actual_registry_slot_lookup_and_source_hook_order(self):
        cc=shutil.which('clang') or shutil.which('cc')
        if not cc:self.skipTest('Native C compiler required')
        preview=self.reconstructed_adapters()
        stage=(SOURCE/'src/gameplay_stage_map.c').read_text()
        slot_type=stage[stage.index('typedef struct MeleeWebStageMapSourceSlot'):stage.index('\n#endif\nstruct MeleeWebStageMap')]
        owner_type=body(stage,'struct MeleeWebStageMap {')+';'
        signatures=('static int fail(', 'static int ok(', 'static int grdatfiles_row_zero(',
            'static int grdatfiles_table_zero(', 'static int grdatfiles_same_row(', 'static int grdatfiles_source_map_matches(',
            'static int grdatfiles_preflight(', 'static int grdatfiles_retire(',
            'static int source_slot_index(', 'static int source_slot_live(',
            'int melee_web_stage_map_set_forms(', 'int melee_web_stage_map_source_slot_bind(',
            'int melee_web_stage_map_source_slot_release(', 'int melee_web_stage_map_source_slot_cleared(',
            'static UnkArchiveStruct* source_slot_table(', 'void* melee_web_stage_map_archives(',
            'void* melee_web_stage_map_lookup(', 'int melee_web_stage_map_close(')
        functions='\n'.join(body(stage,name) for name in signatures)
        bind=body((preview/'src/melee/gr/grdatfiles.c').read_text(),'UnkArchiveStruct* grDatFiles_801C6478(')
        release=body((preview/'src/melee/gr/granime.c').read_text(),'void grAnime_801C65B0(')
        get_archives=body((preview/'src/melee/gr/grdatfiles.c').read_text(),'UnkArchiveStruct* grDatFiles_GetArchive(')
        retire=body((preview/'src/melee/gr/grdatfiles.c').read_text(),'int melee_web_grdatfiles_retire_captured(')

        program=r'''
#include "gameplay_stage_map.h"
#include "gameplay_source_files.h"
#include "gameplay_source_memory_runtime.h"
#include "gameplay_bootstrap_test_stub.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define TARGET_PC 1
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
/* ABI fields and source services are modeled; no raw DAT/header is parsed. */
typedef int s32;
typedef struct HSD_ArchiveHeader {unsigned fixture_header;} HSD_ArchiveHeader;
typedef struct HSD_Archive {unsigned flags;unsigned char* data;void* top_ptr;} HSD_Archive;
typedef struct HSD_GObj {struct HSD_GObj* next;int classifier;void* user_data;} HSD_GObj;
typedef struct Ground {int map_id;} Ground;
enum {HSD_GOBJ_CLASS_STAGE=1};
static HSD_GObj* HSD_GObj_Entities[6];
typedef struct Entry { void* unk0; } Entry;
typedef struct UnkStageDat { int unkC;Entry* unk8; } UnkStageDat;
typedef struct UnkArchiveStruct {HSD_Archive* unk0;UnkStageDat* unk4;unsigned unk8;} UnkArchiveStruct;
struct MeleeWebSourceFileScope {int pending,pumping;};
static struct MeleeWebSourceFileScope file_scope;
static MeleeWebSourceFileScope* active_file=&file_scope;
static uint64_t generation=7,header_generation=21,buffer_generation=22;
static HSD_Archive source_header;static unsigned char source_buffer[0x50000];
static UnkArchiveStruct rows[4];
#define grDatFiles_8049EE10 rows
int melee_web_grdatfiles_retire_captured(const void*,const uint64_t[4]);
static int Ground_801C498C(void){return 0;} /* modeled: no remaining map-light users */
static struct {void* map_gobjs[10];} stage_info; /* modeled ten-entry test maps */
MeleeWebGameplayStats melee_web_gameplay_stats(void){return (MeleeWebGameplayStats){generation};}
int melee_web_gameplay_world_exists(void){return generation!=0;}
MeleeWebSourceFileScope* melee_web_source_files_current_scope(void){return active_file;}
int melee_web_source_files_quiescent_for(MeleeWebSourceFileScope* s,char* e,size_t n){
 if(s!=&file_scope||active_file!=s||s->pending||s->pumping){if(e&&n)snprintf(e,n,"modeled source service busy/foreign");return 0;}return 1;
}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_context_read(MeleeWebSourceMemoryContext* out){
 *out=(MeleeWebSourceMemoryContext){.source_heap_handle=3,.world_generation=generation};return MELEE_WEB_SOURCE_MEMORY_READ_OK;
}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_allocation_read(const void* p,MeleeWebSourceMemoryAllocation* out){
 if(p!=&source_header&&p!=source_buffer)return MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
 *out=(MeleeWebSourceMemoryAllocation){.source_heap_handle=3,.world_generation=generation,
 .allocation_generation=p==&source_header?header_generation:buffer_generation,
 .requested_bytes=p==&source_header?sizeof(source_header):sizeof(source_buffer),.live=1};return MELEE_WEB_SOURCE_MEMORY_READ_OK;
}
UnkArchiveStruct* melee_web_grdatfiles_ordinary_table(void){return rows;}
''' + slot_type+owner_type+r'''
static MeleeWebStageMap* owner;
static const char* const form_files[4]={"GrPs1.dat","GrPs2.dat","GrPs3.dat","GrPs4.dat"};
''' + functions+r'''
static unsigned flags_mutated,header_frees,row_clears,raw_clears,base_releases;
static void* lbHeap_80015BD0(int heap,size_t size){assert(heap==0&&size==sizeof(source_header));return &source_header;}
static void lbArchive_InitializeDAT(HSD_Archive* header,void* data,size_t length){
 assert(header==&source_header&&data==source_buffer&&length==128);
 header->flags=1;header->top_ptr=data;header->data=(unsigned char*)data+sizeof(HSD_ArchiveHeader);
 assert(melee_web_archive_sections_attach_source_buffer(header,"GrPs3.dat",data,length));
}
static void* HSD_ArchiveGetPublicAddress(HSD_Archive* h,const char* name){return melee_web_archive_sections_public(h,name);}
static UnkArchiveStruct* grDatFiles_801C62B4(void){return &rows[1];}
static void grDatFiles_801C6228(UnkStageDat* map){assert(owner->source_slots[1].state==1&&map==owner->form_maps[2]);flags_mutated++;}
#define HSD_ASSERT(line,condition) assert(condition)
static void lbArchive_80016EFC(HSD_Archive* h){
 assert(h!=&source_header&&h==rows[0].unk0&&melee_web_archive_sections_is_handle(h));
 melee_web_archive_sections_release(h);base_releases++; /* modeled original preload release leaf */
}
static void lbHeap_80015CA8(int heap,HSD_Archive* h){assert(heap==0&&h==&source_header&&!melee_web_archive_sections_is_handle(h));header_frees++;}
static void memzero(void* p,size_t n){
 assert(n==sizeof(rows[1]));
 if(p==source_buffer){assert(header_frees==0&&melee_web_archive_sections_is_handle(&source_header));raw_clears++;}
 else{assert((p==&rows[1]||p==&rows[0])&&header_frees==1);row_clears++;}
 memset(p,0,n);
}
''' + bind+release+get_archives+retire+r'''
int main(void){
 char error[256];Entry base_entries[10]={0},entries[4][10]={0};int joints[5];
 UnkStageDat base={10,base_entries},maps[4];void* heads[4];unsigned ids[4]={3,4,9,6};
 base_entries[0].unk0=&joints[0];MeleeWebStageMap* allocated=calloc(1,sizeof(*allocated));assert(allocated);
#define h (*allocated)
 owner=&h;h.generation=generation;
 MeleeWebArchiveSymbol base_symbol={"GrPs.dat","map_head",&base};
 h.sections=melee_web_archive_sections_register(&base_symbol,1,error,sizeof(error));assert(h.sections);
 h.archives[0].unk0=melee_web_archive_sections_open("GrPs.dat");h.archives[0].unk4=&base;
 rows[0]=(UnkArchiveStruct){melee_web_archive_sections_open_preloaded("GrPs.dat"),&base,0};
 h.grdatfiles_rows[0]=rows[0];h.grdatfiles_captured=1;h.grdatfiles_begin_seen=1;h.grdatfiles_tracking=1;
 assert(melee_web_archive_sections_preloaded_stage_map_matches(h.sections,generation,rows[0].unk0,
 h.archives[0].unk0,&base,0,&h.grdatfiles_identities[0],0,&h.grdatfiles_native_identity));
 MeleeWebArchiveSymbol symbols[4];for(unsigned i=0;i<4;i++){
 maps[i]=(UnkStageDat){10,entries[i]};heads[i]=&maps[i];entries[i][ids[i]].unk0=&joints[i+1];symbols[i]=(MeleeWebArchiveSymbol){form_files[i],"map_head",heads[i]};}
 MeleeWebArchiveSections* forms=melee_web_archive_sections_register(symbols,4,error,sizeof(error));assert(forms);
 void* wrong[4]={heads[0],heads[1],heads[2],heads[2]};assert(!melee_web_stage_map_set_forms(&h,wrong,error,sizeof(error)));
 assert(melee_web_stage_map_set_forms(&h,heads,error,sizeof(error)));
 assert(!melee_web_archive_sections_close(forms,error,sizeof(error))); /* real borrower guard */
 assert(!melee_web_stage_map_source_slot_bind(source_buffer,source_buffer,128));
 assert(!melee_web_stage_map_source_slot_release(source_buffer)); /* no foreign dereference */
 assert(grDatFiles_801C6478(source_buffer,128)==&rows[1]&&flags_mutated==1);
 assert(grDatFiles_GetArchive()==rows&&melee_web_stage_map_archives()==rows);
 assert(grDatFiles_GetArchive()[1].unk4==heads[2]);
 assert(melee_web_stage_map_lookup(9)==&rows[1]&&melee_web_stage_map_lookup(0)==&rows[0]);
 uint64_t base_identity=h.grdatfiles_identities[0];h.grdatfiles_identities[0]++;
 assert(!melee_web_stage_map_source_slot_release(&rows[0])&&base_releases==0);
 h.grdatfiles_identities[0]=base_identity;
 uint64_t identity=0;
 assert(!melee_web_archive_sections_source_map_matches(generation,&source_header,h.form_handles[1],heads[2],source_buffer,128,0,&identity));
 assert(!melee_web_archive_sections_source_map_matches(generation,&source_header,h.form_handles[2],heads[2],source_buffer,127,0,&identity));
 assert(!melee_web_archive_sections_source_map_matches(generation,&source_header,h.form_handles[2],heads[2],source_buffer,128,h.source_slots[1].identity+1,&identity));
 for(int kind=0;kind<6;kind++){
  if(kind==0)file_scope.pending=1;if(kind==1)file_scope.pumping=1;if(kind==2)active_file=NULL;
  if(kind==3)buffer_generation++;if(kind==4)header_generation++;if(kind==5)stage_info.map_gobjs[9]=(void*)1;
  assert(!melee_web_stage_map_source_slot_release(&rows[1]));assert(melee_web_archive_sections_is_handle(&source_header)&&header_frees==0);
  file_scope.pending=file_scope.pumping=0;active_file=&file_scope;
  if(kind==3)buffer_generation--;if(kind==4)header_generation--;stage_info.map_gobjs[9]=NULL;
 }
 Ground foreign_ground={9};HSD_GObj linked={NULL,HSD_GOBJ_CLASS_STAGE,&foreign_ground};
 HSD_GObj_Entities[5]=&linked;assert(!melee_web_stage_map_source_slot_release(&rows[1]));HSD_GObj_Entities[5]=NULL;
 source_header.top_ptr=NULL;assert(!melee_web_stage_map_source_slot_release(&rows[1]));source_header.top_ptr=source_buffer;
 UnkArchiveStruct captured=rows[1];rows[1].unk4=&base;assert(!melee_web_stage_map_source_slot_release(&rows[1]));rows[1]=captured;
 assert(!melee_web_stage_map_source_slot_cleared(&rows[1]));
 /* Port-note phase6 raw-buffer branch: actual third word neither0nor1,
  * no header/adjunct free, only original sizeof(slot) prefix invalidation. */
 memset(source_buffer,0x5a,sizeof(UnkArchiveStruct)+1);uint32_t raw_word=929;
 memcpy(source_buffer+offsetof(UnkArchiveStruct,unk8),&raw_word,sizeof(raw_word));
 grAnime_801C65B0((UnkArchiveStruct*)source_buffer);
 assert(raw_clears==1&&header_frees==0&&melee_web_archive_sections_is_handle(&source_header));
 for(size_t i=0;i<sizeof(UnkArchiveStruct);i++)assert(source_buffer[i]==0);
 assert(source_buffer[sizeof(UnkArchiveStruct)]==0x5a&&!h.source_slots[1].raw_clear_pending);
 assert(!melee_web_stage_map_source_slot_release(source_buffer)); /* thirdword now0 */
 source_buffer[0]=0x5a;grAnime_801C65B0(&rows[1]);
 assert(header_frees==1&&row_clears==1&&source_buffer[0]==0x5a&&h.source_slots[1].state==0);
 assert(!melee_web_archive_sections_is_handle(&source_header)&&melee_web_archive_sections_is_handle(h.form_handles[2]));
 assert(!melee_web_archive_sections_close(forms,error,sizeof(error)));
 void* native_handles[4];memcpy(native_handles,h.form_handles,sizeof(native_handles));
 void* base_native=h.archives[0].unk0;void* base_preload=rows[0].unk0;
 assert(melee_web_stage_map_close(owner,error,sizeof(error)));
 assert(owner==NULL&&base_releases==1&&row_clears==2&&melee_web_stage_map_archives()==NULL);
 for(unsigned i=0;i<4;i++)assert(!melee_web_archive_sections_is_handle(native_handles[i]));
 assert(!melee_web_archive_sections_is_handle(base_native)&&!melee_web_archive_sections_is_handle(base_preload));
 assert(melee_web_archive_sections_close(forms,error,sizeof(error)));
 assert(!melee_web_archive_sections_catalog_map_matches("GrPs.dat",&base));
 for(unsigned i=0;i<4;i++)assert(!melee_web_archive_sections_catalog_map_matches(form_files[i],heads[i]));
 puts("actual registry/slot/hooks PASS: exact ordered lookup; owner/lease/pending/Ground negatives; adjunct-before-header-free-before-clear; backing bytes retained");
 return 0;
}
'''
        scratch=Path(tempfile.mkdtemp(prefix='stadium-source-slot-'));passed=False
        try:
            control=scratch/'slot.c';binary=scratch/'slot';control.write_text(program)
            command=[cc,'-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                '-DMELEE_WEB_STADIUM_C1A_DIAGNOSTIC=1','-include',str(REFERENCE/'tests/gameplay_bootstrap_test_stub.h'),
                '-I',str(SOURCE/'src'),'-I',str(REFERENCE/'src'),'-I',str(REFERENCE/'tests'),
                str(SOURCE/'src/gameplay_archive_sections.c'),str(control),'-o',str(binary)]
            for step,argv in enumerate([command,[str(binary)]]):
                result=subprocess.run(argv,capture_output=True,text=True,timeout=60)
                (scratch/f'{step}.stdout').write_text(result.stdout);(scratch/f'{step}.stderr').write_text(result.stderr)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            passed=True
        finally:
            if passed:
                shutil.rmtree(scratch);shutil.rmtree(preview)
            else:
                print(f'Retained source-slot control: {scratch}; adapters: {preview}',flush=True)
