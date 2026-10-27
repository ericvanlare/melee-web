"""Private hash identity controls; no recorder/runtime is initialized."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import unittest
from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(os.environ.get('MELEE_PRIVATE_SHA_REPO_ROOT', Path(__file__).resolve().parents[1]))
SOURCE = Path(os.environ.get('MELEE_PRIVATE_SHA_SOURCE_ROOT', ROOT))

class PrivateSha256Tests(OwnedWorkspaceTests):
    def test_existing_leaf_vectors_and_chunk_boundaries(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            self.skipTest('native C++ compiler unavailable')
        out = self.new_workspace(ROOT, 'private-sha256-')
        cpp = out / 'hash.cpp'
        cpp.write_text(r'''#include "private_sha256.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
int main(int argc,char** argv){
 if(argc!=2)return 2;
 std::ifstream in(argv[1],std::ios::binary);if(!in)return 3;
 melee_web::private_sha256::Sha256 hash;
 char bytes[65];while(in){in.read(bytes,sizeof(bytes));hash.update(reinterpret_cast<const uint8_t*>(bytes),static_cast<std::size_t>(in.gcount()));}
 for(uint8_t byte:hash.finish())std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(byte);
}
''')
        executable = out / 'hash'
        built = subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',
            '-I'+str(SOURCE/'src'),str(cpp),'-o',str(executable)],capture_output=True,text=True,timeout=30)
        (out/'compile.log').write_text(built.stdout+built.stderr)
        self.assertEqual(built.returncode,0,built.stderr)
        for i,data in enumerate([b'',b'abc',b'a'*55,b'a'*56,b'a'*63,b'a'*64,b'a'*65,b'a'*1000000]):
            path=out/('vector-'+str(i));path.write_bytes(data)
            ran=subprocess.run([str(executable),str(path)],capture_output=True,text=True,timeout=30)
            self.assertEqual(ran.returncode,0,ran.stderr)
            self.assertEqual(ran.stdout,hashlib.sha256(data).hexdigest())
