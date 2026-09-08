"""Original font byte storage and checked DOL section mapping."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from extract_font_atlas import font_file_range,FONT_START,FONT_END
from extract_disc_file import DiscFormatError

class FontAtlasTests(unittest.TestCase):
    def test_checked_dol_range(self):
        header=bytearray(0x100)
        def section(i,offset,address,size):
            for position,value in [(i*4,offset),(0x48+i*4,address),(0x90+i*4,size)]:
                struct.pack_into(">I",header,position,value)
        section(8,0x500,FONT_START-0x100,FONT_END-FONT_START+0x100)
        self.assertEqual(font_file_range(header),(0x600,FONT_END-FONT_START))
        section(9,0x600,FONT_START,FONT_END-FONT_START)
        with self.assertRaises(DiscFormatError):font_file_range(header)
        section(9,0,0,0);section(8,0x500,FONT_START,FONT_END-FONT_START-1)
        with self.assertRaises(DiscFormatError):font_file_range(header)
        with self.assertRaises(DiscFormatError):font_file_range(header[:255])

    def test_owned_bytes_and_explicit_absence(self):
        compiler=shutil.which("clang") or shutil.which("cc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            target=Path(directory)/"font_test"
            subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror","-I",str(ROOT/"src"),
                str(ROOT/"src/gameplay_font_atlas.c"),str(ROOT/"tests/gameplay_font_atlas_test.c"),"-o",str(target)],check=True)
            result=subprocess.run([str(target)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(target),"unregistered"],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn("font atlas is not registered",result.stderr)
            local=ROOT/"assets-local/next-gate/sislib_font.bin"
            if local.is_file():
                result=subprocess.run([str(target),str(local)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)

if __name__=="__main__":unittest.main()
