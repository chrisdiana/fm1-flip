"""Export nested mbedTLS archives omitted by pioarduino HybridCompile."""
from pathlib import Path
import hashlib
import shutil

ARCHIVES = {
    'library/libmbedtls.a': 'libmbedtls_2.a',
    'library/libmbedcrypto.a': 'libmbedcrypto.a',
    'library/libmbedx509.a': 'libmbedx509.a',
    '3rdparty/everest/libeverest.a': 'libeverest.a',
    '3rdparty/p256-m/libp256m.a': 'libp256m.a',
}

def export_archives(build, destination):
    nested = Path(build) / 'esp-idf/mbedtls/mbedtls'
    if not nested.is_dir():
        return {}
    sources = {name: nested / relative for relative, name in ARCHIVES.items()}
    for source in sources.values():
        if not source.is_file() or not source.stat().st_size:
            raise RuntimeError('Rebuilt TLS archive missing: ' + str(source))
    Path(destination).mkdir(parents=True, exist_ok=True)
    result = {}
    for name, source in sources.items():
        shutil.copyfile(source, Path(destination) / name)
        result[name] = hashlib.sha256(source.read_bytes()).hexdigest()
    return result
