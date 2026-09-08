from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class ArchiveSectionsTests(unittest.TestCase):
    def test_scoped_sections_and_explicit_missing_services(self):
        with tempfile.TemporaryDirectory() as folder:
            binary=Path(folder)/'sections'
            subprocess.run([shutil.which('clang') or 'cc','-std=gnu11','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'src'),str(ROOT/'src/gameplay_archive_sections.c'),
                str(ROOT/'tests/gameplay_archive_sections_trace.c'),'-o',str(binary)],check=True,capture_output=True)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('restarted',result.stdout)
            for case,message in [('missing','Typed section is not registered'),('handle','Raw archive handles are not supported')]:
                result=subprocess.run([str(binary),case],capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0)
                self.assertIn(message,result.stderr)
