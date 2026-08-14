import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import {dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';

const editorRoot = join(dirname(fileURLToPath(import.meta.url)), '..');

async function sha256(path) {
  const content = await readFile(path);
  return createHash('sha256').update(content).digest('hex');
}

test('Studio browser dependencies are local, complete, and pinned', async () => {
  const html = await readFile(join(editorRoot, 'index.html'), 'utf8');
  const remote = [...html.matchAll(/<(?:script|link)\b[^>]*(?:src|href)=["']([^"']+)["']/gi)]
    .map(match => match[1])
    .filter(url => /^(?:https?:)?\/\//i.test(url));
  assert.deepEqual(remote, [], 'Studio must start without a CDN');

  const sums = await readFile(join(editorRoot, 'vendor', 'SHA256SUMS'), 'utf8');
  const entries = sums.trim().split('\n').map(line => line.trim().split(/\s+/, 2));
  assert.deepEqual(entries.map(([, name]) => name), [
    'peerjs-1.5.5.min.js',
    'ace-1.39.1.js',
    'mode-javascript.js',
    'theme-tomorrow_night_eighties.js',
    'worker-javascript.js',
    'LICENSE.peerjs',
    'LICENSE.ace'
  ]);
  for (const [expected, name] of entries) {
    assert.equal(await sha256(join(editorRoot, 'vendor', name)), expected, `${name} changed`);
  }

  assert.match(html, /vendor\/peerjs-1\.5\.5\.min\.js/);
  assert.match(html, /vendor\/ace-1\.39\.1\.js/);
  assert.match(html, /ace\.config\.set\(['"]basePath['"], ['"]vendor['"]\)/);
  assert.match(html, /href=["']favicon\.svg["']/);
  assert.match(await readFile(join(editorRoot, 'favicon.svg'), 'utf8'), /<svg\b/);
});
