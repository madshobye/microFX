import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const source = await readFile(new URL('../www/portal-app.js', import.meta.url), 'utf8');

function response(body, status = 200) {
  return {ok: status >= 200 && status < 300, status, async json() { return body; }};
}

function snapshot(overrides = {}) {
  return {
    ok: true,
    active: 'demo',
    peerId: 'studio-device',
    activation: {token: 'boot', project: 'demo', state: 'running', detail: 'healthy'},
    network: {state: 'connected', ssid: 'Studio Net', address: '192.0.2.4/24', signal: '-61', bitrate: '72.2', txpower: '15.00'},
    setup: {state: 'healthy', radio: '1', beacon: '1', apMode: '1', link: '1', address: '1', portal: '1'},
    projects: [{id: 'demo', title: 'Demo'}, {id: 'clock', title: 'Clock'}],
    ...overrides
  };
}

function harness(fetch) {
  const elements = new Map();
  for (const id of ['control-status', 'project', 'activate', 'restart', 'net-state', 'net-ssid',
    'net-address', 'net-signal', 'net-bitrate', 'net-power', 'peer-id-input', 'peer-id-status', 'studio-link',
    'setup-state', 'setup-checks', 'net-detail']) {
    elements.set(`#${id}`, {
      textContent: '', value: '', disabled: false, children: [], onclick: null,
      replaceChildren(...children) { this.children = children; this.value = children[0]?.value || ''; }
    });
  }
  const document = {
    querySelector(selector) { return elements.get(selector); },
    createElement() { return {value: '', textContent: ''}; }
  };
  const context = {window: null, document, fetch, MICROFX_PORTAL_NO_AUTO: true, setTimeout, setInterval};
  context.window = context;
  vm.runInNewContext(source, context, {filename: 'portal-app.js'});
  const portal = context.microfxPortal.createPortal({document, fetch, delay: async () => {}, activationAttempts: 4, activationInterval: 0});
  return {portal, elements};
}

test('portal renders projects and both network health surfaces', async () => {
  const {portal, elements} = harness(async () => response(snapshot()));
  await portal.load();
  assert.equal(elements.get('#project').value, 'demo');
  assert.deepEqual(elements.get('#project').children.map(option => option.textContent), ['Demo', 'Clock']);
  assert.equal(elements.get('#control-status').textContent, 'Running: Demo');
  assert.equal(elements.get('#net-state').textContent, 'connected');
  assert.equal(elements.get('#net-ssid').textContent, 'Studio Net');
  assert.equal(elements.get('#net-signal').textContent, '-61 dBm');
  assert.equal(elements.get('#peer-id-status').textContent, 'studio-device');
  assert.equal(elements.get('#peer-id-input').value, 'studio-device');
  assert.equal(elements.get('#studio-link').href, '/studio/?peer=studio-device');
  assert.equal(elements.get('#setup-state').textContent, 'healthy');
  assert.match(elements.get('#setup-checks').textContent, /beacon 1/);
});

test('background refresh preserves an explicit project selection', async () => {
  const {portal, elements} = harness(async () => response(snapshot()));
  await portal.load();
  elements.get('#project').value = 'clock';
  await portal.load();
  assert.equal(elements.get('#project').value, 'clock');
});

test('Run project waits for acknowledged renderer health and unlocks controls', async () => {
  const calls = [];
  const replies = [
    response({ok: true, activation: 'portal-42'}),
    response(snapshot({activation: {token: 'portal-42', project: 'clock', state: 'starting', detail: ''}})),
    response(snapshot({active: 'clock', activation: {token: 'portal-42', project: 'clock', state: 'running', detail: 'healthy'}}))
  ];
  const {portal, elements} = harness(async (url, options = {}) => {
    calls.push({url, options});
    return replies.shift();
  });
  elements.get('#project').value = 'clock';
  await portal.request('activate', 'clock');
  assert.equal(calls[0].options.body, 'action=activate&project=clock');
  assert.equal(elements.get('#control-status').textContent, 'Running: Clock');
  assert.equal(elements.get('#activate').disabled, false);
  assert.equal(elements.get('#restart').disabled, false);
  assert.equal(portal.isBusy(), false);
});

test('renderer failure is visible and leaves controls recoverable', async () => {
  const replies = [
    response({ok: true, activation: 'portal-bad'}),
    response(snapshot({activation: {token: 'portal-bad', project: 'demo', state: 'failed', detail: 'shader compile failed'}}))
  ];
  const {portal, elements} = harness(async () => replies.shift());
  await assert.rejects(portal.request('restart'), /shader compile failed/);
  assert.equal(elements.get('#control-status').textContent, 'shader compile failed');
  assert.equal(elements.get('#restart').disabled, false);
});

test('HTTP and malformed device responses become actionable UI errors', async () => {
  let malformed = false;
  const {portal, elements} = harness(async () => malformed
    ? {ok: true, status: 200, async json() { throw new Error('bad json'); }}
    : response({ok: false, message: 'project missing'}, 400));
  await assert.rejects(portal.load(), /project missing/);
  assert.equal(elements.get('#control-status').textContent, 'project missing');
  malformed = true;
  await assert.rejects(portal.load(), /Device returned 200/);
  assert.equal(elements.get('#control-status').textContent, 'Device returned 200');
});
