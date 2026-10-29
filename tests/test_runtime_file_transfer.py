"""Compile the real transaction/value/adapter leaves; no world or DVD callback."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('MELEE_WEB_CANDIDATE_SOURCE_ROOT',ROOT))
REFERENCE=Path(os.environ.get('MELEE_WEB_REFERENCE_SOURCE_ROOT',ROOT))

class RuntimeFileTransferTests(unittest.TestCase):
    def compile_control(self,source):
        compiler=shutil.which('clang++') or shutil.which('c++')
        if not compiler:self.skipTest('C++20 compiler required')
        scratch=Path(tempfile.mkdtemp(prefix='runtime-file-transfer-'));passed=False
        try:
            include=scratch/'include';(include/'dolphin').mkdir(parents=True)
            (include/'dolphin/pad.h').write_text('typedef struct PADStatus PADStatus;\n')
            # Use the actual separately owned C input declaration, not a
            # duplicated test struct, when composing external candidates.
            api=Path(os.environ.get('MELEE_SOURCE_FILE_API_HEADER',SOURCE/'src/gameplay_source_files.h'))
            shutil.copyfile(api,include/'gameplay_source_files.h')
            if 'ACTUAL_NATIVE_TRANSFER_EXPORT' in source.read_text():
                native=(SOURCE/'src/gameplay_menu_browser_menu.cpp').read_text()
                start=native.index('int melee_web_native_asset_file_transfer(')
                end=native.index('int melee_web_native_asset_commit(',start)
                actual=source.read_text().replace('// ACTUAL_NATIVE_TRANSFER_EXPORT',native[start:end])
                source=scratch/'actual-control.cpp';source.write_text(actual)
            command=[compiler,'-std=c++20','-Wall','-Wextra','-Werror','-O1',
                     '-DMELEE_WEB_RUNTIME_ASSET_SCOPE_TESTING','-I',str(include),
                     '-I',str(SOURCE/'src'),'-I',str(REFERENCE/'src'),str(source),'-o',str(scratch/'control')]
            for index,argv in enumerate([command,[str(scratch/'control')]]):
                result=subprocess.run(argv,capture_output=True,text=True,timeout=60)
                (scratch/f'{index}.stdout').write_text(result.stdout);(scratch/f'{index}.stderr').write_text(result.stderr)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            passed=True
        finally:
            if passed:shutil.rmtree(scratch)
            else:print(f'Retained runtime-file transfer control: {scratch}',flush=True)

    def test_actual_transaction_tail_and_input_adapter(self):
        self.compile_control(ROOT/'tests/runtime_file_transfer_test.cpp')

    def test_existing_logical_asset_scope_guarantees(self):
        self.compile_control(REFERENCE/'tests/runtime_asset_scope_test.cpp')


    def test_disc_transfer_node_controls(self):
        import sys
        sys.path.insert(0,str(REFERENCE/'scripts'))
        from check_gameplay import node_runtime
        if not (REFERENCE/'.deps/emsdk/.emscripten').is_file():
            self.skipTest('Bootstrap the pinned Node runtime')
        result=subprocess.run([str(node_runtime()),'--test',str(ROOT/'tests/disc_source_transfer.test.mjs')],
                              cwd=ROOT,capture_output=True,text=True,timeout=60)
        print(result.stdout,end='',flush=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__':unittest.main()
