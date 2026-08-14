import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';

const root = new URL('../www/', import.meta.url);
const html = (await readFile(new URL('index.html', root), 'utf8'))
  .replaceAll('@PRODUCT_NAME@', 'microFX fixture')
  .replaceAll('@SETUP_SSID@', 'microFX-setup')
  .replaceAll('@SETUP_PASSWORD@', 'fixture-only');
const script = await readFile(new URL('portal-app.js', root));
let active = 'demo';
let sequence = 0;
let activation = {token: 'fixture-boot', project: active, state: 'running', detail: 'fixture renderer healthy'};
let pendingReads = 0;

function status() {
  if (activation.state === 'starting' && ++pendingReads >= 2) {
    activation = {...activation, state: 'running', detail: 'fixture renderer healthy'};
  }
  return {
    ok: true,
    active,
    activation,
    network: {state: 'connected', detail: 'fixture client link', ssid: 'microFX Lab', address: '192.0.2.4/24', signal: '-58', bitrate: '72.2', txpower: '15.00'},
    setup: {state: 'healthy', radio: '1', beacon: '1', apMode: '1', link: '1', address: '1', portal: '1', failures: '0'},
    projects: [{id: 'demo', title: 'Bundled demo'}, {id: 'clock', title: 'Electricity clock'}]
  };
}

const server = createServer(async (request, reply) => {
  if (request.url === '/' || request.url === '/index.html') {
    reply.writeHead(200, {'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store'});
    reply.end(html);
    return;
  }
  if (request.url === '/portal-app.js') {
    reply.writeHead(200, {'Content-Type': 'text/javascript; charset=utf-8', 'Cache-Control': 'no-store'});
    reply.end(script);
    return;
  }
  if (request.url === '/cgi-bin/control' && request.method === 'GET') {
    reply.writeHead(200, {'Content-Type': 'application/json', 'Cache-Control': 'no-store'});
    reply.end(JSON.stringify(status()));
    return;
  }
  if (request.url === '/cgi-bin/control' && request.method === 'POST') {
    let body = '';
    for await (const chunk of request) body += chunk;
    const fields = new URLSearchParams(body);
    const action = fields.get('action');
    const project = action === 'restart' ? active : fields.get('project');
    if (!['activate', 'restart'].includes(action) || !['demo', 'clock'].includes(project)) {
      reply.writeHead(400, {'Content-Type': 'application/json'});
      reply.end(JSON.stringify({ok: false, message: 'invalid fixture request'}));
      return;
    }
    active = project;
    pendingReads = 0;
    activation = {token: `fixture-${++sequence}`, project, state: 'starting', detail: ''};
    reply.writeHead(200, {'Content-Type': 'application/json', 'Cache-Control': 'no-store'});
    reply.end(JSON.stringify({ok: true, message: 'renderer activation requested', activation: activation.token}));
    return;
  }
  reply.writeHead(404, {'Content-Type': 'text/plain'});
  reply.end('not found');
});

server.listen(0, '127.0.0.1', () => {
  const address = server.address();
  console.log(`http://127.0.0.1:${address.port}/`);
});
