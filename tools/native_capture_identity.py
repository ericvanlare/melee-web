"""Content identities for the executable and owned asset trees of a native run."""
import hashlib
from pathlib import Path


def file_identity(path):
    path = Path(path)
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return {'sha256': digest.hexdigest(), 'bytes': path.stat().st_size}


def tree_identity(root):
    root = Path(root).resolve(strict=True)
    if not root.is_dir():
        raise ValueError('Native asset input must be a directory')
    files = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink() and path.is_dir():
            raise ValueError('Native asset directory symlinks cannot be inventoried implicitly')
        if path.is_file():
            files[path.relative_to(root).as_posix()] = file_identity(path)
        elif not path.is_dir():
            raise ValueError('Native asset tree contains a non-regular entry')
    return files


def runtime_inputs(node, menu, game):
    return {'node': file_identity(node), 'menu_assets': tree_identity(menu),
            'game_assets': tree_identity(game)}
