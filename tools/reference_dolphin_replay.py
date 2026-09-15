"""Prepare a new original replay without changing the finalized source bundle."""
from __future__ import annotations
import json
import hashlib
from pathlib import Path
from reference_capture_environment import file_inventory
from reference_input_stream import validate_stream, validate_status
from reference_session_bundle import _final_manifest, validate_bundle

class DolphinReplayError(ValueError):
    pass

def load_source(path):
    path = Path(path).expanduser()
    if path.is_symlink() or not path.is_dir(): raise DolphinReplayError('Choose a finalized capture bundle folder')
    path = path.resolve()
    if not (path/'inputs.mwri').is_file():
        raise DolphinReplayError('This capture predates Dolphin input recording. Its web-port comparison remains usable; faithful Dolphin replay needs a new recording.')
    manifest, errors = _final_manifest(path)
    if errors or manifest is None: raise DolphinReplayError('The source capture manifest is invalid: '+'; '.join(errors))
    report = validate_bundle(path, require_complete=True)
    if not report.valid: raise DolphinReplayError('The source capture failed validation: '+'; '.join(report.errors))
    stream = validate_stream(path/'inputs.mwri')
    validate_status(path/'input-status.json', mode='record', events=stream['events'])
    profile = read_profile(path/'configuration-snapshot.json')
    configuration = json.loads((path/'configuration.json').read_text())
    if {name:hashlib.sha256(data).hexdigest() for name,data in profile.items()} != configuration.get('profile_files'):
        raise DolphinReplayError('The captured Dolphin configuration does not match its receipt')
    header = json.loads((path/'header.json').read_text())
    return {'path':path,'profile':profile,'input_path':path/'inputs.mwri',
            'manifest_sha256':manifest['manifest_sha256'],'input':stream,'header':header}

def verify_replay_environment(source, identity):
    expected = source['header']['environment']
    for key in ('disc','dolphin','prepared_fixture_sha256','prepared_fixture_kind','timing_policy','locale','os'):
        if expected.get(key) != identity.get(key):
            raise DolphinReplayError('Dolphin replay requires the original '+key.replace('_',' ')+' identity')
    return True


def snapshot_profile(profile):
    inventory = file_inventory(profile)
    data = {name:(Path(profile)/name).read_bytes().hex() for name in inventory}
    if sum(len(value)//2 for value in data.values()) > 1024*1024:
        raise DolphinReplayError('Captured configuration exceeds its bound')
    return {'version':1,'files':data}

def read_profile(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 3*1024*1024:
        raise DolphinReplayError('Captured configuration is missing or exceeds its bound')
    value = json.loads(path.read_text())
    if not isinstance(value,dict) or set(value) != {'version','files'} or value['version'] != 1 or not isinstance(value['files'],dict):
        raise DolphinReplayError('Captured configuration schema is invalid')
    result = {}
    for name, data in value['files'].items():
        relative=Path(name)
        if (not isinstance(name,str) or not name or relative.is_absolute() or
                any(part in ('.','..') for part in name.split('/')) or '\\' in name or
                not isinstance(data,str)):
            raise DolphinReplayError('Captured configuration path is unsafe')
        try: result[name]=bytes.fromhex(data)
        except ValueError as error: raise DolphinReplayError('Captured configuration bytes are malformed') from error
    if not result or sum(map(len,result.values())) > 1024*1024:
        raise DolphinReplayError('Captured configuration is empty or exceeds its bound')
    return result
