import http.server
import json
import os
import pathlib
import subprocess
import tempfile
import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from urllib.parse import unquote

BIN = str(pathlib.Path(__file__).resolve().parents[1] / 'build/mhc')

def graph(multi=False, cycle=False):
    p = {
        '日本 空格/#': {'type': 'Shadowsocks'},
        '美 国': {'type': 'VMess'},
        'DIRECT': {'type': 'Direct'},
        'Sub': {'type': 'Selector', 'all': ['日本 空格/#', '美 国'], 'now': '美 国'},
        'GLOBAL': {'type': 'Selector', 'all': ['Sub'], 'now': 'Sub'},
    }
    if multi:
        p['Other'] = {'type': 'Selector', 'all': ['日本 空格/#'], 'now': '日本 空格/#'}
    if cycle:
        p['Sub']['all'].append('GLOBAL')
    return {'proxies': p}

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def send(self, code, obj=None):
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        if obj is not None:
            try: self.wfile.write(json.dumps(obj, ensure_ascii=False).encode())
            except (BrokenPipeError, ConnectionResetError): pass
    def auth(self):
        if self.headers.get('Authorization') != 'Bearer test':
            self.send(401, {'error': 'unauthorized'})
            return False
        return True
    def do_GET(self):
        if not self.auth(): return
        path = unquote(self.path.split('?')[0])
        state = self.server.state
        if path == '/configs' and state.get('config_error'):
            kind = state['config_error']
            if kind == 'timeout': time.sleep(5.3)
            elif kind == 'empty': self.send(200); return
            elif kind == 'bad_json':
                self.send_response(200); self.end_headers(); self.wfile.write(b'{'); return
            elif kind == 'not_found': self.send(404, {}); return
        if path == '/configs': self.send(200, {'mode': state['mode']})
        elif path == '/proxies': self.send(200, state['graph'])
        elif path == '/providers/proxies': self.send(200, state.get('providers', {'providers': {}}))
        elif path.startswith('/providers/proxies/') and path.endswith('/healthcheck'):
            self.send(200, {'delay': 37})
        elif path.endswith('/delay'):
            name = path[len('/proxies/'):-len('/delay')]
            if name == '美 国': self.send(500, {'error': 'failed'})
            else: self.send(200, {'delay': 42})
        elif path.startswith('/proxies/'):
            name = path[len('/proxies/'):]
            self.send(200, state['graph']['proxies'].get(name, {}))
        else: self.send(404, {})
    def do_PUT(self):
        if not self.auth(): return
        group = unquote(self.path[len('/proxies/'):])
        name = json.loads(self.rfile.read(int(self.headers['Content-Length'])))['name']
        self.server.state['puts'].append((group, name))
        if group in self.server.state['fail']: self.send(500, {}); return
        if group not in self.server.state.get('drift', set()):
            self.server.state['graph']['proxies'][group]['now'] = name
        self.send(204)
    def do_PATCH(self):
        if not self.auth(): return
        self.server.state['mode'] = json.loads(self.rfile.read(int(self.headers['Content-Length'])))['mode']
        self.send(204)

class CliTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.server.state = {'graph': graph(), 'puts': [], 'fail': set(), 'mode': 'rule'}
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True); self.thread.start()
        self.env = dict(os.environ, MHC_API=f'http://127.0.0.1:{self.server.server_port}', MHC_SECRET='test', XDG_CACHE_HOME=self.tmp.name)
    def tearDown(self):
        self.server.shutdown(); self.server.server_close(); self.thread.join(); self.tmp.cleanup()
    def run_cli(self, *args, env=None):
        return subprocess.run([BIN, *args], env=env or self.env, text=True, capture_output=True, timeout=15)
    def test_status_mode_and_input(self):
        self.assertEqual(self.run_cli('status').stdout, 'Mode: rule\n')
        self.assertEqual(self.run_cli('mode', 'invalid').returncode, 2)
        self.assertEqual(self.run_cli('mode', 'global').returncode, 0)
        self.assertEqual(self.run_cli('status').stdout, 'Mode: global\n')
    def test_auth_and_connection(self):
        env = dict(self.env, MHC_SECRET='wrong')
        r=self.run_cli('status', env=env); self.assertNotEqual(r.returncode, 0); self.assertIn('401', r.stderr); self.assertEqual(r.stdout, '')
        env=dict(self.env, MHC_API='http://127.0.0.1:1')
        r=self.run_cli('status', env=env); self.assertNotEqual(r.returncode, 0); self.assertIn('connection', r.stderr)
    def test_http_and_json_errors(self):
        for kind, message in [('empty','invalid or empty JSON'), ('bad_json','invalid or empty JSON'), ('not_found','HTTP 404'), ('timeout','timeout')]:
            with self.subTest(kind=kind):
                self.server.state['config_error']=kind
                r=self.run_cli('status');self.assertNotEqual(r.returncode,0);self.assertIn(message,r.stderr);self.assertEqual(r.stdout,'')
        self.server.state.pop('config_error')
    def test_nodes_and_route(self):
        self.assertEqual(self.run_cli('nodes').stdout.splitlines(), ['日本 空格/#', '美 国'])
        r=self.run_cli('use', '日本 空格/#'); self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.server.state['puts'], [('Sub','日本 空格/#'), ('GLOBAL','Sub')])
    def test_ambiguous_route_and_group(self):
        self.server.state['graph']=graph(multi=True)
        r=self.run_cli('use','日本 空格/#'); self.assertNotEqual(r.returncode,0); self.assertEqual(self.server.state['puts'],[])
        r=self.run_cli('use','日本 空格/#','--group','Other'); self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(self.server.state['puts'],[('Other','日本 空格/#')])
    def test_cycle_and_partial_mutation(self):
        self.server.state['graph']=graph(cycle=True)
        self.assertNotEqual(self.run_cli('use','日本 空格/#').returncode,0)
        self.assertEqual(self.server.state['puts'],[])
        self.server.state['graph']=graph();self.server.state['fail'].add('GLOBAL')
        r=self.run_cli('use','日本 空格/#');self.assertNotEqual(r.returncode,0);self.assertIn('partial change',r.stderr)
    def test_remote_state_change(self):
        self.server.state['drift']={'Sub'}
        r=self.run_cli('use','日本 空格/#')
        self.assertNotEqual(r.returncode,0)
        self.assertIn('remote selection differs',r.stderr)
        self.assertEqual(self.server.state['puts'],[('Sub','日本 空格/#')])
    def test_probe_partial_and_cache(self):
        r=self.run_cli('testall');self.assertNotEqual(r.returncode,0)
        self.assertIn('日本 空格/#: 42 ms',r.stdout)
        self.assertIn('美 国: error',r.stdout)
        cache=list(pathlib.Path(self.tmp.name,'mhc').glob('*.json'))
        self.assertEqual(len(cache),1)
        r=self.run_cli('use','国');self.assertEqual(r.returncode,0,r.stderr)
    def test_noninteractive_ambiguous(self):
        self.server.state['graph']['proxies']['美 国2']={'type':'VMess'}
        r=self.run_cli('use','国');self.assertNotEqual(r.returncode,0);self.assertIn('no interactive terminal',r.stderr)
    def test_cache_expired_corrupt_and_clear(self):
        self.server.state['graph']['proxies']['日本 2']={'type':'Shadowsocks'}
        self.run_cli('test','日本')
        cache=list(pathlib.Path(self.tmp.name,'mhc').glob('*.json'))[0]
        cache.write_text('{broken')
        r=self.run_cli('use','日本');self.assertNotEqual(r.returncode,0)
        cache.write_text(json.dumps({'日本 空格/#':{'time':time.time()-4000,'state':'valid','delay':1}}))
        r=self.run_cli('use','日本');self.assertNotEqual(r.returncode,0)
        self.assertEqual(self.run_cli('clear').returncode,0)
        self.assertFalse(cache.exists())
    def test_concurrent_cache_updates(self):
        with ThreadPoolExecutor(max_workers=4) as pool:
            results=list(pool.map(lambda _: self.run_cli('test','日本'), range(4)))
        self.assertTrue(all(r.returncode==0 for r in results))
        cache=list(pathlib.Path(self.tmp.name,'mhc').glob('*.json'))[0]
        self.assertIn('日本 空格/#',json.loads(cache.read_text()))
    def test_more_than_256_nodes(self):
        for i in range(300):
            self.server.state['graph']['proxies'][f'node-{i}']={'type':'Shadowsocks'}
        r=self.run_cli('nodes')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(len(r.stdout.splitlines()),302)
    def test_provider_nodes(self):
        self.server.state['providers']={'providers':{'subscription':{'proxies':[
            {'name':'订阅 节点/#','type':'Shadowsocks'}]}}}
        self.server.state['graph']['proxies']['Sub']['all'].append('订阅 节点/#')
        r=self.run_cli('nodes');self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn('订阅 节点/#',r.stdout)
        r=self.run_cli('test','订阅 节点/#');self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn('37 ms',r.stdout)
        r=self.run_cli('use','订阅 节点/#','--group','Sub')
        self.assertEqual(r.returncode,0,r.stderr)

if __name__ == '__main__': unittest.main()
