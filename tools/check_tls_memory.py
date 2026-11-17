#!/usr/bin/env python3
"""Check the SDK-level Cardputer TLS memory policy, not app-only compiler flags."""
from pathlib import Path
import configparser, argparse, subprocess, json, hashlib
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--elf",type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
config=configparser.ConfigParser(interpolation=None);config.read(root/'platformio.ini')
settings={line.split('=',1)[0].strip():line.split('=',1)[1].strip() for line in config['env:cardputer']['custom_sdkconfig'].splitlines() if '=' in line}
assert settings.get('CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN')=='y','Cardputer still reserves symmetric 16 KB TLS buffers (32 KB total)'
assert settings.get('CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN')=='16384','Keep support for full-sized incoming TLS records'
assert int(settings['CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN'])<=4096
assert settings.get('CONFIG_MBEDTLS_SSL_PROTO_DTLS')=='n', 'IDF dynamic TLS buffers require disabling unused DTLS'
assert settings.get('CONFIG_MBEDTLS_DYNAMIC_BUFFER')=='y','Release TLS record buffers when unused'
print('Cardputer SDK TLS policy: full 16 KB receive records, <=4 KB send, dynamic allocation')

# HybridCompile drops CMake interface linker flags when exporting its archives.
# Ensure the final Arduino app activates the actual dynamic-buffer wrappers.
required=('mbedtls_ssl_write_client_hello','mbedtls_ssl_handshake_client_step',
 'mbedtls_ssl_tls13_handshake_client_step','mbedtls_ssl_handshake_server_step',
 'mbedtls_ssl_read','mbedtls_ssl_write','mbedtls_ssl_session_reset','mbedtls_ssl_free',
 'mbedtls_ssl_setup','mbedtls_ssl_send_alert_message','mbedtls_ssl_close_notify')
flags=config['env:cardputer']['build_flags']
for name in required:
 assert '--wrap='+name in flags, 'Missing IDF TLS dynamic-buffer linker hook: '+name
for path in ('src/device/app.cpp','src/device/catalogue.cpp'):
 source=(root/path).read_text()
 assert 'ca.isEmpty()?FM1_TRUST_ROOTS:ca.c_str()' in source
 assert 'ca=FM1_TRUST_ROOTS' not in source and 'return FM1_TRUST_ROOTS' not in source
print('Verified IDF dynamic-buffer linker hooks and zero-copy built-in CA text')

if args.elf:
 sdk=Path.home()/'.platformio/packages/framework-arduinoespressif32-libs/esp32s3'
 resolved=(sdk/'qio_qspi/include/sdkconfig.h').read_text()
 for definition in ('#define CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN 16384',
                    '#define CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN 4096',
                    '#define CONFIG_MBEDTLS_DYNAMIC_BUFFER 1'):
  assert definition in resolved, 'Resolved SDK differs from configured TLS policy: '+definition
 assert '#define CONFIG_MBEDTLS_SSL_PROTO_DTLS ' not in resolved
 hashes=json.loads((root/'.cache/tls-sdk-cardputer.json').read_text())
 assert len(hashes)==5
 for name,digest in hashes.items():
  assert hashlib.sha256((sdk/'lib'/name).read_bytes()).hexdigest()==digest, 'Stale TLS SDK archive: '+name
 print('Resolved SDK settings and all five rebuilt TLS archive hashes verified')
 nm=Path.home()/'.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm'
 symbols=subprocess.check_output([str(nm),str(args.elf)],text=True)
 for name in ('mbedtls_ssl_setup','mbedtls_ssl_read','mbedtls_ssl_write','mbedtls_ssl_free'):
  assert ' T __wrap_'+name+'\n' in symbols, 'Dynamic TLS hook missing from final app: '+name
 print('Final executable contains active dynamic TLS setup/read/write/free hooks')
