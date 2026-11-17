#!/usr/bin/env python3
"""Run the production connection routine against deterministic USB peer traces."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
source = (root/'src/transport/device_transport.cpp').read_text()
connect = source[source.index('bool connectDevice('):source.index('bool backupDevice(')]
connect = connect.replace('.isEmpty()', '.empty()')
with tempfile.TemporaryDirectory(prefix='fm1-connect-') as name:
    path = Path(name)
    (path/'main.cpp').write_text((root/'test/transport_flow_adapter.hpp').read_text()+'\n'+connect+'\n'+(root/'test/transport_flow_main.hpp').read_text())
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Iinclude', str(path/'main.cpp'), '-o', str(path/'test')], cwd=root, check=True)
    subprocess.run([str(path/'test')], cwd=root, check=True)

# Execute the complete production install orchestrator; the USB peer, filesystem,
# parser and writer boundaries are fakes. Native core tests cover parser/writer.
headers = next((root/'.pio/libdeps').glob('*/ArduinoJson/src/ArduinoJson.h'), None)
if not headers:
    raise SystemExit('Install PlatformIO firmware dependencies first')
install = source[source.index('bool installDevice('):source.index('bool restoreDevice(')]
install = install.replace('selected.startsWith("/fm1/packages/")', '(selected.rfind("/fm1/packages/",0)==0)')
install = install.replace('selected.endsWith(".fwsc")', '(selected.size()>=5 && selected.compare(selected.size()-5,5,".fwsc")==0)')
install = install.replace('selected.indexOf("..")>=0', 'selected.find("..")!=String::npos')
install = install.replace('expected.equalsIgnoreCase(hash)', '(expected==hash)')
install = install.replace('.isEmpty()', '.empty()').replace('String(report.address,HEX)', 'std::to_string(report.address)').replace('String(changed)', 'std::to_string(changed)')
with tempfile.TemporaryDirectory(prefix='fm1-install-') as name:
    path = Path(name)
    (path/'main.cpp').write_text((root/'test/transport_flow_adapter.hpp').read_text()+'\n'+connect+'\n'+(root/'test/install_flow_adapter.hpp').read_text()+'\n'+install+'\n'+(root/'test/install_flow_main.hpp').read_text())
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Iinclude', '-I'+str(headers.parent), str(path/'main.cpp'), '-o', str(path/'test')], cwd=root, check=True)
    subprocess.run([str(path/'test')], cwd=root, check=True)

usb = (root/'src/transport/uboot_host.cpp').read_text()
callback = usb[usb.index('static void client_cb('):usb.index('static void client_task(')]
handoff = usb[usb.index('bool wait_reconnect('):usb.index('// Runs one transfer and waits for it.')]
with tempfile.TemporaryDirectory(prefix='fm1-usb-handoff-') as name:
    path = Path(name)
    (path/'main.cpp').write_text((root/'test/usb_handoff_adapter.hpp').read_text()+'\n'+callback+'\n'+handoff+'\n'+(root/'test/usb_handoff_main.hpp').read_text())
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(path/'main.cpp'), '-o', str(path/'test')], cwd=root, check=True)
    subprocess.run([str(path/'test')], cwd=root, check=True)

# Exercise the production atomic cancellation gate in every operation phase.
policy=source[source.index('bool transportCanCancel()'):source.index('uint64_t transportBackupReserve()')]
with tempfile.TemporaryDirectory(prefix='fm1-cancel-gate-') as name:
    path=Path(name)
    (path/'main.cpp').write_text('#include <atomic>\n#include <cstdint>\n#include <cassert>\nstd::atomic<uint8_t> cancellation{0};\n'+policy+\
        '\nint main(){for(uint8_t phase=0;phase<5;++phase){cancellation.store(phase);assert(transportCanCancel()==(phase==1||phase==3));assert(transportCancel()==(phase==1||phase==2||phase==3));}}')
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(path/'main.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
    print('Production cancellation gate: preflight/write accepted, idle/reboot rejected')
