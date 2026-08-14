(function(global) {
  'use strict';

  function createPortal(options) {
    const document = options.document;
    const fetch = options.fetch;
    const delay = options.delay || (milliseconds => new Promise(resolve => global.setTimeout(resolve, milliseconds)));
    const activationAttempts = options.activationAttempts || 30;
    const activationInterval = options.activationInterval === undefined ? 500 : options.activationInterval;
    const status = document.querySelector('#control-status');
    const picker = document.querySelector('#project');
    const activateButton = document.querySelector('#activate');
    const restartButton = document.querySelector('#restart');
    let busy = false;
    let loaded = false;

    function setBusy(value) {
      busy = value;
      activateButton.disabled = value;
      restartButton.disabled = value;
      picker.disabled = value;
    }

    async function json(response) {
      let result;
      try { result = await response.json(); }
      catch (_) { throw new Error(`Device returned ${response.status || 'an invalid response'}`); }
      if (!response.ok || !result.ok) throw new Error(result.message || `Device returned HTTP ${response.status}`);
      return result;
    }

    function render(result) {
      const projects = Array.isArray(result.projects) ? result.projects : [];
      const previous = loaded ? picker.value : '';
      picker.replaceChildren(...projects.map(project => {
        const option = document.createElement('option');
        option.value = project.id;
        option.textContent = project.title || project.id;
        return option;
      }));
      const preferred = projects.some(project => project.id === previous) ? previous : result.active;
      if (preferred) picker.value = preferred;
      loaded = true;

      const activation = result.activation || {};
      const activeProject = projects.find(project => project.id === result.active);
      status.textContent = activation.state === 'failed'
        ? `Failed: ${activation.detail || 'renderer rejected the project'}`
        : result.active ? `Running: ${activeProject?.title || result.active}` : 'No active project';

      const network = result.network || {};
      document.querySelector('#net-state').textContent = network.state || 'unknown';
      document.querySelector('#net-ssid').textContent = network.ssid || '—';
      document.querySelector('#net-address').textContent = network.address || '—';
      document.querySelector('#net-signal').textContent = network.signal ? `${network.signal} dBm` : '—';
      document.querySelector('#net-bitrate').textContent = network.bitrate ? `${network.bitrate} Mbit/s` : '—';
      document.querySelector('#net-power').textContent = network.txpower ? `${network.txpower} dBm` : '—';
      const peerId = result.peerId || 'microfx-demo';
      document.querySelector('#peer-id-status').textContent = peerId;
      document.querySelector('#peer-id-input').value = peerId;
      document.querySelector('#studio-link').href = `/studio/?peer=${encodeURIComponent(peerId)}`;
      const setup = result.setup || {};
      document.querySelector('#setup-state').textContent = setup.state || 'unknown';
      document.querySelector('#setup-checks').textContent = `radio ${setup.radio || 0} · beacon ${setup.beacon || 0} · AP ${setup.apMode || 0} · link ${setup.link || 0} · IP ${setup.address || 0} · HTTP ${setup.portal || 0}`;
      document.querySelector('#net-detail').textContent = network.detail || '';
      return result;
    }

    async function load() {
      try {
        const response = await fetch('/cgi-bin/control', {cache: 'no-store'});
        return render(await json(response));
      } catch (error) {
        status.textContent = error.message;
        throw error;
      }
    }

    async function waitForActivation(token) {
      for (let attempt = 0; attempt < activationAttempts; attempt++) {
        const result = await load();
        const activation = result.activation || {};
        if (activation.token === token && activation.state === 'running') return result;
        if (activation.token === token && activation.state === 'failed') {
          throw new Error(activation.detail || 'Renderer failed');
        }
        await delay(activationInterval);
      }
      throw new Error('Renderer start timed out');
    }

    async function request(action, project = '') {
      if (busy) return;
      setBusy(true);
      try {
        const response = await fetch('/cgi-bin/control', {
          method: 'POST',
          headers: {'Content-Type': 'application/x-www-form-urlencoded'},
          body: `action=${action}&project=${encodeURIComponent(project)}`
        });
        const result = await json(response);
        status.textContent = 'Starting renderer…';
        await waitForActivation(result.activation);
      } catch (error) {
        status.textContent = error.message;
        throw error;
      } finally {
        setBusy(false);
      }
    }

    activateButton.onclick = () => request('activate', picker.value).catch(() => {});
    restartButton.onclick = () => request('restart').catch(() => {});
    return {load, render, request, waitForActivation, isBusy: () => busy};
  }

  global.microfxPortal = {createPortal};
  if (global.document && !global.MICROFX_PORTAL_NO_AUTO) {
    const portal = createPortal({document: global.document, fetch: global.fetch.bind(global)});
    global.microfxPortal.instance = portal;
    portal.load().catch(() => {});
    global.setInterval(() => portal.load().catch(() => {}), 5000);
  }
})(typeof window === 'undefined' ? globalThis : window);
