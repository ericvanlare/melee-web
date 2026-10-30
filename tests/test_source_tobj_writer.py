"""Discovery shim; behavioral source controls stay in GPL profile scope."""
import importlib.util
from pathlib import Path
_p=Path(__file__).resolve().parents[1]/"src/slippi_profile/tests/test_tobj_writer.py"
_s=importlib.util.spec_from_file_location("profile_tobj_writer_tests",_p)
_m=importlib.util.module_from_spec(_s);_s.loader.exec_module(_m)
TObjWriterTests=_m.TObjWriterTests
