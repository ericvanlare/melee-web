"""Real single-thread semantics; unavailable services must terminate by name."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class GameplayPlatformTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Use the build's checked generated source; never regenerate shared trees
        # from this SDK-boundary test while another build could be active.
        source=ROOT/'build/gameplay-source/src'
        if not (source/'Runtime/platform.h').is_file():
            raise unittest.SkipTest('Run gameplay source preparation first')
        compiler=shutil.which('clang') or shutil.which('cc')
        if not compiler: raise RuntimeError('A C11 compiler is required')
        cls.temp=tempfile.TemporaryDirectory(prefix='melee platform ')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary=Path(cls.temp.name)/'platform'
        args=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-DTARGET_PC','-I',str(ROOT/'src'),'-I',str(source),'-I',str(ROOT/'.deps/aurora/include'),'-I',str(ROOT/'.deps/melee/extern/dolphin/include'),'-include',str(ROOT/'src/gameplay_compat.h'),str(ROOT/'src/gameplay_platform.c'),str(ROOT/'tests/gameplay_platform_test.c'),'-o',str(cls.binary)]
        result=subprocess.run(args,capture_output=True,text=True,timeout=120)
        if result.returncode: raise RuntimeError(result.stdout+result.stderr)
    def test_nested_interrupt_restore_and_cache_coherence(self):
        result=subprocess.run([str(self.binary),'interrupts'],capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_unavailable_services_never_return_or_invoke_success_callback(self):
        cases={'card':'CARDMountAsync','card-create':'CARDCreateAsync','dvd':'DVDConvertPathToEntrynum','audio':'AXAcquireVoice','video':'VIGetNextField','transfer':'HSD_DevComRequest'}
        for mode,name in cases.items():
            with self.subTest(operation=name):
                result=subprocess.run([str(self.binary),mode],capture_output=True,text=True,timeout=20)
                self.assertNotEqual(result.returncode,0)
                self.assertIn('Unsupported gameplay platform operation: '+name,result.stderr)
                self.assertNotIn('UNEXPECTED_',result.stdout+result.stderr)
    def test_osreport_and_osvreport_preserve_format_arguments(self):
        result=subprocess.run([str(self.binary),'report'],capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(result.stdout,'')
        self.assertEqual(result.stderr,'report 7 ok 1.50\nvreport 11 ok\n')
    def test_ospanic_reports_location_and_aborts(self):
        result=subprocess.run([str(self.binary),'panic'],capture_output=True,text=True,timeout=20)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('PANIC platform-test.c:37: panic 5 boom\n',result.stderr)
