"""Execute the native scoped-import boundaries used by single-match replay."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def block(source, marker):
    start = source.index(marker)
    brace = source.index('{', start)
    depth = 0
    for index in range(brace, len(source)):
        depth += (source[index] == '{') - (source[index] == '}')
        if not depth:
            return source[start:index + 1]
    raise AssertionError(marker)


class ReplayScopedImportLifecycleTests(unittest.TestCase):
    def test_native_import_mode_and_replay_asset_route(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        core = (ROOT / 'src/gameplay_menu_browser.cpp').read_text()
        menu = (ROOT / 'src/gameplay_menu_browser_menu.cpp').read_text()
        replay = (ROOT / 'src/gameplay_menu_browser_match.cpp').read_text()
        close_assets = block(block(core, 'void close()'), 'if(scoped_assets)')
        replay_close = block(replay, 'if(!candidate->whole_session())')
        advance_replay = block(core, 'if(scoped_assets){request_assets(AssetDestination::Replay')
        begin = block(menu, 'unsigned melee_web_native_asset_begin()')
        clear = block(menu, 'int melee_web_native_source_files_external_clear()')
        file = block(menu, 'int melee_web_native_menu_file(')
        source = r'''
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
bool scoped_disc_import=false, scoped_assets=false, asset_committed=false;
bool world=false,match=false,results=false,prize=false,host_entered=false;
bool asset_selection_valid=false,asset_opening_preview_valid=false;
unsigned asset_generation=0;
std::string message;
std::vector<std::string> requested_assets;
std::unique_ptr<int> archive_cache;
std::map<std::string,std::vector<uint8_t>> files;
const char* keys[]={"PlCo.dat"};
const char* route_asset_keys[]={"PlCo.dat"};
const char* zelda_sheik_keys[]={"PlCo.dat"};
enum class AssetDestination {None,InitialMenu,Replay};
AssetDestination asset_destination=AssetDestination::None;
struct Scope {
 unsigned pending=0,releases=0,aborts=0;
 unsigned pending_generation(){return pending;}
 void abort(unsigned){++aborts;pending=0;}
 void release(){++releases;files.clear();}
} asset_scope;
void check(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
bool clear_ok=true;
int melee_web_source_files_external_clear(char*,unsigned){return clear_ok;}
void close(){
''' + close_assets + r'''
 world=match=results=prize=host_entered=false;
}
void request_assets(AssetDestination destination, const int* selection=nullptr){
 assert(!world&&!match&&!results&&!prize&&!host_entered);
 if(destination==AssetDestination::Replay)assert(selection);
 asset_destination=destination;asset_generation=++asset_scope.pending;
 requested_assets={destination==AssetDestination::Replay?"PlCo.dat":"CSS"};
}
''' + begin + '\n' + clear + '\n' + file + r'''
struct Recipe {bool whole=false;int selection=8;bool whole_session(){return whole;}};
void select_replay(Recipe* candidate){
''' + replay_close + r'''
}
Recipe* replay;
bool eager_constructed=false;
void advance(){
''' + advance_replay + r'''
 eager_constructed=true;
}
int main(){
 Recipe single,whole;whole.whole=true;
 assert(melee_web_native_asset_begin()!=0);
 files["CSS"]={1};archive_cache=std::make_unique<int>(1);
 close();
 assert(scoped_disc_import&&!scoped_assets&&files.empty()&&!archive_cache);
 assert(asset_scope.releases==1&&asset_scope.aborts==1);
 replay=&single;select_replay(replay);advance();
 assert(scoped_assets&&asset_destination==AssetDestination::Replay);
 assert(requested_assets==std::vector<std::string>{"PlCo.dat"}&&!eager_constructed);
 close();clear_ok=false;
 assert(!melee_web_native_source_files_external_clear()&&scoped_disc_import);
 clear_ok=true;
 assert(melee_web_native_source_files_external_clear()&&!scoped_disc_import);
 select_replay(&single);advance();assert(eager_constructed&&!scoped_assets);
 // Explicit legacy file import selects eager mode, with real input bytes.
 scoped_disc_import=true;uint8_t data[]={1,2};
 assert(melee_web_native_menu_file("PlCo.dat",data,2)&&!scoped_disc_import);
 assert(files["PlCo.dat"]==std::vector<uint8_t>({1,2}));
 // Whole-session selection retains the prepared owner and active scope.
 assert(melee_web_native_asset_begin());world=true;host_entered=true;
 auto releases=asset_scope.releases;select_replay(&whole);
 assert(world&&host_entered&&scoped_assets&&scoped_disc_import);
 assert(asset_scope.releases==releases);
}
'''
        with tempfile.TemporaryDirectory(prefix='melee-replay-scope-') as directory:
            cpp = Path(directory) / 'test.cpp'
            exe = Path(directory) / 'test'
            cpp.write_text(source)
            subprocess.run([compiler, '-std=c++20', str(cpp), '-o', str(exe)], check=True,
                           capture_output=True, text=True)
            subprocess.run([str(exe)], check=True, capture_output=True, text=True)


if __name__ == '__main__':
    unittest.main()
