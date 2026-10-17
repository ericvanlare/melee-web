"""Asset-free control of the exact patched authored-selection accessor."""
import os
from pathlib import Path
import shlex
import subprocess
import unittest
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function

ROOT = Path(os.environ.get('MELEE_WEB_SSS_PREFIX_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
UPSTREAM = Path(os.environ.get('MELEE_WEB_SSS_PREFIX_UPSTREAM', ROOT / '.deps/melee'))

class SelectedStageAccessorTests(OwnedWorkspaceTests):
    def test_actual_authored_table_and_accessor(self):
        static = UPSTREAM / 'src/melee/mn/mnstagesel.static.h'
        if 'MELEE_WEB_SSS_PREFIX_UPSTREAM' in os.environ:
            self.assertTrue(os.environ['MELEE_WEB_SSS_PREFIX_UPSTREAM'].strip(),
                            'configured pinned Melee source must be nonempty')
            self.assertTrue(static.is_file(), 'configured pinned Melee source is missing')
        elif not static.is_file():
            self.skipTest('pinned Melee source is not prepared')
        patch = (ROOT / 'patches/melee-gameplay.patch').read_text()
        section = patch.split('diff --git a/src/melee/mn/mnstagesel.c ', 1)[1].split('\ndiff --git ', 1)[0]
        added = '\n'.join(line[1:] for line in section.splitlines() if line.startswith('+') and not line.startswith('+++'))
        accessor = _function(added, 'int melee_web_sss_selected_stage(')
        source = static.read_text()
        begin = source.index('struct stagelistinfo {')
        end = source.index('\n};', begin) + 3
        table = source[begin:end]
        control = '''#include <assert.h>\n#include <stdint.h>\n#include <stdio.h>\ntypedef void HSD_JObj;typedef unsigned char u8;typedef float f32;\n'''
        control += table + '\nstatic u8 mnStageSel_804D6CAE;\n' + accessor
        control += r'''
int main(void) {
 int index=-9,kind=-8;
 for(unsigned i=0;i<sizeof(mnStageSel_803F06D0)/sizeof(mnStageSel_803F06D0[0]);++i){
  mnStageSel_804D6CAE=i;assert(melee_web_sss_selected_stage(&index,&kind));
  assert(index==(int)i && kind==mnStageSel_803F06D0[i].xB);
 }
 mnStageSel_804D6CAE=18;assert(melee_web_sss_selected_stage(&index,&kind));assert(index==18&&kind==3);
 index=-9;kind=-8;assert(!melee_web_sss_selected_stage(NULL,&kind));assert(kind==-8);
 assert(!melee_web_sss_selected_stage(&index,NULL));assert(index==-9);
 for(unsigned i=30;i<256;++i){mnStageSel_804D6CAE=i;assert(!melee_web_sss_selected_stage(&index,&kind));assert(index==-9&&kind==-8);}
 puts("PASS actual authored table/accessor: 30 rows, 226 invalid indexes, null outputs, no transform/RNG");
 return 0;
}
'''
        work = self.new_workspace(ROOT, 'sss-selected-stage-')
        c = work / 'control.c'; binary = work / 'control'; c.write_text(control)
        command = shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(binary)]
        (work/'command.json').write_text(__import__('json').dumps(command))
        run = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (work/'compile.log').write_text(run.stdout); self.assertEqual(run.returncode, 0, run.stdout)
        run = subprocess.run([str(binary)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (work/'run.log').write_text(run.stdout); self.assertEqual(run.returncode, 0, run.stdout)
        self.assertIn('PASS actual authored table/accessor', run.stdout)

if __name__ == '__main__': unittest.main()
