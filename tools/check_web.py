#!/usr/bin/env python3
"""Exercise the real web UI in headless Chrome against a local fixture API.

No device, Hub request, download or flash operation is performed. This checks
browser event wiring, pagination, filters, safe text rendering and queued actions.
"""
import argparse
import http.server
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import urllib.parse

ROOT = Path(__file__).resolve().parents[1]
DRIVER = r'''<script>
(async()=>{
 const wait=async check=>{for(let i=0;i<500;i++){if(check())return;await new Promise(r=>setTimeout(r,20))}throw Error('Browser condition timed out')};
 const assert=(x,m)=>{if(!x)throw Error(m)};
 try{
  await wait(()=>document.querySelectorAll('#list article').length===16);
  assert(!document.querySelector('#list img[onerror]')&&!window.fixtureXss,'Metadata was interpreted as markup');
  assert(document.querySelector('#sort').value==='popular','Popularity must be the default sort');
  assert(document.querySelector('#list h3').textContent==='Firmware 31','Default popularity order was not requested');
  assert(document.querySelector('#list img'),'Maker artwork missing');
  document.querySelector('#next').click();await wait(()=>document.querySelector('#cache').textContent.includes('showing 17'));
  document.querySelector('#query').value='needle description';document.querySelector('#query').dispatchEvent(new Event('input'));
  await wait(()=>document.querySelector('#cache').textContent.startsWith('1 results'));
  assert(document.querySelector('#list h3').textContent==='Firmware 07','Description search failed');
  document.querySelector('#query').value='';document.querySelector('#query').dispatchEvent(new Event('input'));
  await wait(()=>document.querySelector('#cache').textContent.startsWith('32 results'));
  document.querySelector('#release').value='beta';document.querySelector('#release').dispatchEvent(new Event('change'));
  await wait(()=>document.querySelector('#cache').textContent.startsWith('16 results'));
  document.querySelector('#category').value='effect';document.querySelector('#category').dispatchEvent(new Event('change'));
  await wait(()=>document.querySelector('#cache').textContent.startsWith('8 results'));
  document.querySelector('#sort').value='newest';document.querySelector('#sort').dispatchEvent(new Event('change'));
  await wait(()=>document.querySelector('#list h3')?.textContent==='Firmware 28');
  document.querySelector('#list article button').click();await wait(()=>document.querySelector('#detail').open);
  assert(document.querySelector('#detailbody a[rel="noopener noreferrer"]'),'Maker links missing');
  [...document.querySelectorAll('#detailbody button')].find(b=>b.textContent==='Download to device').click();
  await wait(()=>document.querySelector('#message').textContent.includes('queued'));
  document.querySelector('#detail').close();
  document.querySelector('[data-view=library]').click();
  await wait(()=>document.querySelectorAll('#library-list article').length===2);
  assert(document.querySelector('#library-list').textContent.includes('Legacy saved firmware'),'Saved releases missing outside catalogue');
  document.querySelector('[data-view=device]').click();
  await wait(()=>document.querySelector('#device-current').textContent.includes('Last identified'));
  assert(!document.querySelector('#device-current').textContent.includes('Live identity confirmed'),'Cached firmware falsely shown live');
  assert(document.querySelector('#device-history').textContent.includes('No confirmed installations'),'Download falsely became installation history');
  [...document.querySelectorAll('nav button')].find(b=>b.textContent==='Files & backups').click();
  await wait(()=>[...document.querySelectorAll('#list button')].some(b=>b.textContent==='Select for restore'));
  [...document.querySelectorAll('#list button')].find(b=>b.textContent==='Select for restore').click();
  await wait(()=>document.querySelector('#message').textContent.includes('physical confirmation'));
  document.documentElement.dataset.testResult='PASS';
 }catch(e){document.documentElement.dataset.testResult='FAIL';document.documentElement.dataset.testError=e.message;}
})();</script>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
    parser.add_argument('--preview',type=Path,help='Save a gallery screenshot using local fixture artwork')
    args = parser.parse_args()
    records=[]
    for i in range(32):
        records.append(dict(id=f'fixture-{i}',name=f'Firmware {i:02}',author='Fixture maker',
            description=('needle description' if i==7 else '<img src=x onerror="window.fixtureXss=true"> plain metadata'),
            version='1.0',date=f'2026-11-{i+1:02}',tags=['effect' if i%4<2 else 'synth'],
            beta=i%2==0,chip='new',compatibility='unknown',size=1234,saved=False,
            downloadable=True,favorite=False,license='GPL-3.0-only',hubInstalls=(i+1)*10,artworkUrl='/fixture-card.svg' if i%2 else None,
            sourceUrl='https://github.com/maker/project',hubUrl='https://thefirmwarehub.com/'))
    requests=[]
    library=[dict(records[0],saved=True),dict(records[1],id='local-legacy',name='Legacy saved firmware',version='0.7',saved=True,local=True)]
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*_): pass
        def reply(self,payload,status=200,kind='application/json'):
            data=payload.encode() if isinstance(payload,str) else json.dumps(payload).encode()
            self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        def do_GET(self):
            url=urllib.parse.urlsplit(self.path);query=urllib.parse.parse_qs(url.query)
            val=lambda key,default='':query.get(key,[default])[0]
            if url.path=='/': return self.reply((ROOT/'web/index.html').read_text() if args.preview else (ROOT/'web/index.html').read_text().replace('</html>',DRIVER+'</html>'),kind='text/html')
            if url.path=='/fixture-card.svg':return self.reply('<svg xmlns="http://www.w3.org/2000/svg" width="640" height="360"><rect width="640" height="360" fill="#244c55"/><circle cx="450" cy="180" r="130" fill="#73c7ae"/><path d="M0 250L640 100M0 270L640 120M0 290L640 140" stroke="#d4e0bb" stroke-width="3"/><text x="35" y="95" font-family="sans-serif" font-size="32" fill="white">TEST ARTWORK</text></svg>',kind='image/svg+xml')
            if url.path=='/api/library':return self.reply(dict(entries=library,total=len(library),storageLabel='fixture switcher'))
            if url.path=='/api/device':return self.reply(dict(current=dict(firmwareProduct='FM-1_918',firmwareKnown=True,firmwareLive=False,mode='disconnected'),record=dict(lastObserved=dict(product='FM-1_918'),history=[])))
            if url.path=='/api/status': return self.reply(dict(stage='Fixture',detail='No hardware attached',storageReady=True,free=4000000,completed=0,total=0,canCancel=False,busy=False,targetFlashId=0,ap='127.0.0.1',station='127.0.0.1'))
            if url.path=='/api/files': return self.reply([dict(path='/fm1/backups/backup-1.bin',size=1048576,deletableBackup=False)])
            if url.path=='/api/catalogue':
                rows=[r for r in records if val('q').lower() in ' '.join([r['name'],r['author'],r['description'],*r['tags']]).lower()]
                if val('release') in ('beta','stable'): rows=[r for r in rows if r['beta']==(val('release')=='beta')]
                if val('category','all')!='all': rows=[r for r in rows if val('category') in r['tags']]
                if val('compatibility','all')!='all': rows=[r for r in rows if r['compatibility']==val('compatibility')]
                if val('filter')=='favorite': rows=[r for r in rows if r['favorite']]
                if val('filter')=='saved' or val('view')=='saved':rows=[r for r in rows if r['saved']]
                rows=sorted(rows,key=lambda r:r['date'] if val('sort')=='newest' else r['hubInstalls'] if val('sort')=='popular' else r['name'],reverse=val('sort') in ('newest','popular'))
                offset=int(val('offset','0'))
                return self.reply(dict(entries=rows[offset:offset+16],total=len(rows),categories=['effect','synth'],retrievedAt='fixture',sourceUrl='https://thefirmwarehub.com/catalog.json',attribution='The Firmware Hub',license='CC-BY-4.0',targetFlashId=0))
            return self.reply('Not found',404,'text/plain')
        def do_POST(self):
            body=self.rfile.read(int(self.headers.get('Content-Length',0))).decode()
            requests.append((self.path,urllib.parse.parse_qs(body)))
            if self.path=='/api/download': return self.reply('Download queued for fixture only',202,'text/plain')
            if self.path=='/api/restore': return self.reply('Restore armed; physical confirmation required',202,'text/plain')
            return self.reply('Unexpected action',400,'text/plain')
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix='fm1-web-check-') as profile:
            preview_flags=[]
            if args.preview:
                args.preview.parent.mkdir(parents=True,exist_ok=True)
                preview_flags=['--screenshot='+str(args.preview.resolve()),'--window-size=1440,1150']
            run=subprocess.run([args.browser,*preview_flags,'--headless','--disable-gpu','--no-first-run','--disable-default-apps','--disable-background-networking',f'--user-data-dir={profile}','--virtual-time-budget=15000','--dump-dom',f'http://127.0.0.1:{server.server_port}/'],capture_output=True,text=True,timeout=45)
            if args.preview:
                if run.returncode or not args.preview.is_file():raise SystemExit('Browser preview failed: '+run.stderr[-1000:])
                print('Gallery preview: '+str(args.preview));return
            if run.returncode or 'data-test-result="PASS"' not in run.stdout:
                error=run.stdout[run.stdout.find('data-test-result'):run.stdout.find('data-test-result')+250]
                raise SystemExit('Web browser fixture failed: '+error+' '+run.stderr[-1000:])
        assert any(path=='/api/download' for path,_ in requests)
        assert any(path=='/api/restore' for path,_ in requests)
        assert all(path in ('/api/download','/api/restore') for path,_ in requests)
        print('Browser passed: default popularity, artwork, pagination, search/filters, separate downloaded library, stale-vs-live firmware, empty confirmed history, queued actions and safe metadata')
    finally:
        server.shutdown();server.server_close()

if __name__=='__main__': main()
