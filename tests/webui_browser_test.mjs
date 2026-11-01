import assert from 'node:assert/strict';
import {spawn, spawnSync} from 'node:child_process';
import {mkdtemp, readFile, rm, stat} from 'node:fs/promises';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {setTimeout as delay} from 'node:timers/promises';
import {fileURLToPath} from 'node:url';

const chromePath = process.env.LLC_CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', 'module', 'webroot');
const cleanupToken = 'a'.repeat(64);
const candidateKey = 'candidate-good-key';
const previousKey = 'previous-good-key';
const injectedMarkup = '<img src=x onerror=window.__xss=1>';

function serveLocalWebRoot() {
  const server = http.createServer(async (request, response) => {
    try {
      const pathname = decodeURIComponent(new URL(request.url, 'http://127.0.0.1').pathname);
      const relativePath = pathname === '/' ? 'index.html' : pathname.replace(/^\/+/, '');
      const filePath = path.resolve(webRoot, relativePath);
      if (filePath !== webRoot && !filePath.startsWith(`${webRoot}${path.sep}`)) {
        response.writeHead(403).end('forbidden');
        return;
      }
      const contents = await readFile(filePath);
      const extension = path.extname(filePath);
      const contentType = extension === '.html' ? 'text/html; charset=utf-8'
        : extension === '.css' ? 'text/css; charset=utf-8'
          : extension === '.mjs' ? 'text/javascript; charset=utf-8' : 'application/octet-stream';
      response.writeHead(200, {'content-type': contentType, 'cache-control': 'no-store'});
      response.end(contents);
    } catch {
      response.writeHead(404).end('not found');
    }
  });
  return server;
}

function cdpClient(socket) {
  let nextId = 1;
  const pending = new Map();
  socket.addEventListener('message', (event) => {
    const message = JSON.parse(typeof event.data === 'string' ? event.data : Buffer.from(event.data).toString('utf8'));
    if (!message.id) return;
    const entry = pending.get(message.id);
    if (!entry) return;
    pending.delete(message.id);
    clearTimeout(entry.timer);
    if (message.error) entry.reject(new Error(`CDP ${message.error.code}: ${message.error.message}`));
    else entry.resolve(message.result);
  });
  return {
    send(method, params = {}, sessionId) {
      const id = nextId++;
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          pending.delete(id);
          reject(new Error(`CDP command timed out: ${method}`));
        }, 10000);
        pending.set(id, {resolve, reject, timer});
        const message = {id, method, params};
        if (sessionId) message.sessionId = sessionId;
        socket.send(JSON.stringify(message));
      });
    },
    close() { socket.close(); },
  };
}

async function waitForDevToolsPort(profilePath, chrome) {
  const activePortFile = path.join(profilePath, 'DevToolsActivePort');
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    if (chrome.exitCode !== null) throw new Error(`Chrome exited early with code ${chrome.exitCode}`);
    try {
      const lines = (await readFile(activePortFile, 'utf8')).trim().split(/\r?\n/);
      const port = Number(lines[0]);
      if (Number.isInteger(port) && port > 0 && lines[1]?.startsWith('/devtools/browser/')) {
        return {port, wsPath: lines[1]};
      }
    } catch {}
    await delay(50);
  }
  throw new Error('Timed out waiting for DevToolsActivePort');
}

async function connectChrome(chrome, profilePath) {
  const {port, wsPath} = await waitForDevToolsPort(profilePath, chrome);
  const socket = new WebSocket(`ws://127.0.0.1:${port}${wsPath}`);
  await new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, {once: true});
    socket.addEventListener('error', () => reject(new Error('Could not connect to Chrome DevTools')), {once: true});
  });
  return cdpClient(socket);
}

async function evaluate(cdp, sessionId, expression) {
  const response = await cdp.send('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
    userGesture: true,
  }, sessionId);
  if (response.exceptionDetails) {
    throw new Error(response.exceptionDetails.exception?.description ?? response.exceptionDetails.text);
  }
  return response.result?.value;
}

async function waitFor(cdp, sessionId, expression, predicate = Boolean, label = expression) {
  const deadline = Date.now() + 10000;
  let lastValue;
  while (Date.now() < deadline) {
    lastValue = await evaluate(cdp, sessionId, expression);
    if (predicate(lastValue)) return lastValue;
    await delay(40);
  }
  throw new Error(`Timed out waiting for ${label}; last value: ${JSON.stringify(lastValue)}`);
}

function fixtureScript() {
  return `(() => {
    const calls = [];
    const state = {staleRuntime: false, delays: {check: 0, progress: 0}, network: {mode: 'system', port: 7890, effective_source: 'system'}, activeFont: 'default'};
    const candidateKey = ${JSON.stringify(candidateKey)};
    const previousKey = ${JSON.stringify(previousKey)};
    const cleanupToken = ${JSON.stringify(cleanupToken)};
    const injectedMarkup = ${JSON.stringify(injectedMarkup)};
    window.__bridgeCalls = calls;
    window.__makeRuntimeStale = () => { state.staleRuntime = true; };
    const makeStatus = () => ({
      kind: 'module_manager_status', schema: 1, busy: false,
      local_resources: {state: 'read', mode: 'active', version: '123', pack_key: 'disk-pack-key'},
      resource_store: {state: 'managed', active: 'disk-pack-key', previous: previousKey, network: state.network},
      network: {...state.network},
      fonts: {
        active_font: state.activeFont === 'default' ? '默认思源黑体' : state.activeFont,
        is_custom: state.activeFont !== 'default',
        available: [
          {name: '默认思源黑体', path: 'default', is_active: state.activeFont === 'default', size_bytes: 0},
          {name: 'TestFont.bundle', path: '/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/fonts/TestFont.bundle', is_active: state.activeFont === 'TestFont.bundle', size_bytes: 123456}
        ]
      },
      updates: {state: 'no_update', downloads: {state: 'not_started'}, last_success: {remote: {version: '123'}, checked_at_unix: 1793300000}},
      downloads: {state: 'not_started', prepared: {key: candidateKey, files: 2, uncompressed_bytes: 2048, plan: {version: '124', size: 512}}, last_attempt: null},
      runtime: {
        fresh_request: !state.staleRuntime,
        state: 'active',
        report: {
          kind: 'localization_status', pid: 24680,
          resources: {local_version: '123', pack_key: 'running-pack-key'},
          coverage: {missing_local_rows: state.staleRuntime ? 7777 : 3, unsupported_source_fields: 1, state: 'module_coverage_limited'}
        }
      }
    });
    window.ksu = {exec(command, optionsJson, callbackName) {
      const call = {command, optionsJson, callbackName, startedAt: performance.now()};
      calls.push(call);
      let result;
      if (command.endsWith(' status --json') || command.endsWith(' check --json') || command.endsWith(" activate '${candidateKey}' --json") || command.endsWith(" rollback '${previousKey}' --json") || command.endsWith(' import-current --json')) {
        result = makeStatus();
      } else if (command.endsWith(' cleanup-plan --json')) {
        result = {kind: 'module_cleanup_plan', schema: 1, token: cleanupToken, bytes: 1024, items: [{id: injectedMarkup, bytes: 1024}]};
      } else if (command.endsWith(' cleanup ' + cleanupToken + ' --json')) {
        result = {kind: 'module_cleanup_result', schema: 1, phase: 'succeeded', deleted: [injectedMarkup]};
      } else if (command.endsWith(" set-font 'TestFont.bundle' --json")) {
        state.activeFont = 'TestFont.bundle';
        result = makeStatus();
      } else if (command.endsWith(' network 12345 --json')) {
        state.network = {mode: 'loopback', port: 12345, effective_source: 'module_setting'};
        result = makeStatus();
      } else if (command.endsWith(' progress --json')) {
        result = {kind: 'module_manager_progress', schema: 1, busy: false, downloads: {state: 'not_started'}};
      } else {
        result = {kind: 'module_manager_error', schema: 1, error: 'Unexpected test command'};
      }
      const delayMs = command.endsWith(' check --json') ? state.delays.check
        : command.endsWith(' progress --json') ? state.delays.progress : 5;
      setTimeout(() => {
        call.completedAt = performance.now();
        window[callbackName](0, JSON.stringify(result), '');
      }, delayMs);
    }};
    window.__bridgeState = state;
  })();`;
}

test('WebUI works in a 360px headless Chrome and routes actions through the bridge', async (t) => {
  await stat(chromePath);
  const temporaryRoot = await mkdtemp(path.join(os.tmpdir(), 'llc-webui-browser-test-'));
  const profilePath = path.join(temporaryRoot, 'chrome-profile');
  const server = serveLocalWebRoot();
  let chrome;
  let cdp;
  let sessionId;
  let serverListening = false;

  try {
    await new Promise((resolve, reject) => {
      server.once('error', reject);
      server.listen(0, '127.0.0.1', resolve);
    });
    serverListening = true;
    const address = server.address();
    const pageUrl = `http://127.0.0.1:${address.port}/`;
    chrome = spawn(chromePath, [
      '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
      '--disable-extensions', '--disable-background-networking', '--disable-sync', '--metrics-recording-only',
      '--disable-breakpad', '--disable-crash-reporter',
      '--remote-debugging-port=0', `--user-data-dir=${profilePath}`, '--remote-allow-origins=*',
      '--window-size=480,900', 'about:blank',
    ], {windowsHide: true, stdio: 'ignore'});
    cdp = await connectChrome(chrome, profilePath);
    const {targetId} = await cdp.send('Target.createTarget', {url: 'about:blank'});
    const attached = await cdp.send('Target.attachToTarget', {targetId, flatten: true});
    sessionId = attached.sessionId;
    await cdp.send('Page.enable', {}, sessionId);
    await cdp.send('Runtime.enable', {}, sessionId);
    await cdp.send('Emulation.setDeviceMetricsOverride', {
      width: 360, height: 800, deviceScaleFactor: 1, mobile: true,
    }, sessionId);

    await cdp.send('Page.navigate', {url: pageUrl}, sessionId);
    const noBridge = await waitFor(cdp, sessionId,
      `(() => ({banner: document.getElementById('banner')?.textContent, disabled: document.getElementById('check')?.disabled, width: document.documentElement.scrollWidth, innerWidth}))()`,
      (value) => value?.banner?.includes('没有 Root 桥接'), 'no-bridge warning');
    assert.equal(noBridge.disabled, true, 'actions stay disabled without the manager bridge');
    assert.equal(noBridge.innerWidth, 360);
    assert.ok(noBridge.width <= noBridge.innerWidth, `no-bridge layout overflows: ${noBridge.width}`);

    await cdp.send('Page.addScriptToEvaluateOnNewDocument', {source: fixtureScript()}, sessionId);
    await cdp.send('Page.navigate', {url: `${pageUrl}?fixture=bridge`}, sessionId);
    const initial = await waitFor(cdp, sessionId,
      `(() => ({calls: window.__bridgeCalls?.map(x => x.command) ?? [], disk: document.getElementById('disk-version')?.textContent, banner: document.getElementById('banner')?.textContent, width: document.documentElement.scrollWidth, innerWidth, running: document.getElementById('running-version')?.textContent, note: document.getElementById('switch-note')?.textContent, coverage: document.getElementById('coverage')?.textContent, coverageNote: document.getElementById('coverage-note')?.textContent}))()`,
      (value) => value?.calls?.length === 1 && value.disk === '123', 'initial status report');
    assert.deepEqual(initial.calls, ['/data/adb/modules/llc_zygisk/bin/llc-manager status --json']);
    assert.equal(initial.disk, '123');
    assert.equal(initial.running, '123', 'fixture has equal disk and runtime versions');
    assert.match(initial.note, /下次冷启动/);
    assert.match(initial.coverage, /缺少本地条目 3/);
    assert.match(initial.coverageNote, /部分源字段尚未适配/);
    assert.equal(initial.width <= initial.innerWidth, true, `fixture layout overflows: ${initial.width}`);
    assert.equal(initial.calls.some((command) => command.includes(' check --json')), false, 'opening the page must not check remote updates');

    await evaluate(cdp, sessionId, `window.__bridgeState.delays.check = 2200; window.__bridgeState.delays.progress = 500; document.getElementById('check').click()`);
    const afterCheck = await waitFor(cdp, sessionId,
      `(() => ({calls: window.__bridgeCalls, banner: document.getElementById('banner').textContent}))()`,
      (value) => value.calls.some((call) => call.command.endsWith(' check --json')) && value.calls.some((call) => call.command.endsWith(' progress --json') && call.completedAt !== undefined) && value.banner.includes('更新检查已完成'),
      'check completion and delayed progress');
    assert.ok(afterCheck.calls.some((call) => call.command === '/data/adb/modules/llc_zygisk/bin/llc-manager check --json'));
    assert.doesNotMatch(afterCheck.banner, /清理/);
    const completedCheck = afterCheck.calls.find((call) => call.command.endsWith(' check --json'));
    const completedProgress = afterCheck.calls.find((call) => call.command.endsWith(' progress --json'));
    assert.ok(completedProgress.completedAt > completedCheck.completedAt,
      'progress must arrive after the check result');
    assert.equal(afterCheck.calls.filter((call) => call.command.endsWith(' status --json')).length, 2,
      'late progress must not trigger a second status request after the action has completed');

    await evaluate(cdp, sessionId, `window.__makeRuntimeStale(); document.getElementById('refresh').click()`);
    const stale = await waitFor(cdp, sessionId,
      `(() => ({calls: window.__bridgeCalls.map(x => x.command), coverage: document.getElementById('coverage').textContent, note: document.getElementById('coverage-note').textContent}))()`,
      (value) => value.calls.filter((command) => command.endsWith(' status --json')).length >= 3 && value.coverage.includes('需要当前进程'),
      'stale runtime refresh');
    assert.doesNotMatch(`${stale.coverage} ${stale.note}`, /7777/);
    assert.match(stale.note, /过期报告不会作为当前运行证据/);

    await evaluate(cdp, sessionId, `document.getElementById('activate').click()`);
    const activatePrompt = await waitFor(cdp, sessionId,
      `(() => ({open: document.getElementById('confirm-dialog').open, description: document.getElementById('confirm-description').textContent}))()`,
      (value) => value.open, 'activate confirmation dialog');
    assert.match(activatePrompt.description, new RegExp(candidateKey));
    await evaluate(cdp, sessionId, `document.getElementById('confirm-accept').click()`);
    const afterActivate = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command.includes(`activate '${candidateKey}' --json`)), 'activate command');
    assert.ok(afterActivate.some((command) => command === `/data/adb/modules/llc_zygisk/bin/llc-manager activate '${candidateKey}' --json`));

    const readyAfterActivate = await waitFor(cdp, sessionId,
      `({calls: window.__bridgeCalls.map(x => x.command), ready: !document.getElementById('rollback').disabled, banner: document.getElementById('banner').textContent})`,
      (value) => value.ready && value.banner.includes('磁盘资源已提交'), 'activation completion');
    const beforeRollback = readyAfterActivate.calls.length;
    await evaluate(cdp, sessionId, `document.getElementById('rollback').click()`);
    const rollbackPrompt = await waitFor(cdp, sessionId,
      `(() => ({open: document.getElementById('confirm-dialog').open, description: document.getElementById('confirm-description').textContent}))()`,
      (value) => value.open, 'rollback confirmation dialog');
    assert.match(rollbackPrompt.description, new RegExp(previousKey));
    await evaluate(cdp, sessionId, `document.getElementById('confirm-cancel').click()`);
    await waitFor(cdp, sessionId, `document.getElementById('confirm-dialog').open`, (open) => !open, 'rollback dialog dismissal');
    await delay(80);
    const afterCancel = await evaluate(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`);
    assert.equal(afterCancel.length, beforeRollback, 'cancelling rollback must not execute a command');
    assert.equal(afterCancel.some((command) => command.includes(' rollback ')), false);

    await evaluate(cdp, sessionId, `document.getElementById('proxy-enabled').click(); document.getElementById('proxy-port').value = '70000'; document.getElementById('save-network').click()`);
    const invalidNetwork = await waitFor(cdp, sessionId,
      `({banner: document.getElementById('banner').textContent, calls: window.__bridgeCalls.map(x => x.command)})`,
      (value) => value.banner.includes('1–65535'), 'invalid proxy port warning');
    assert.equal(invalidNetwork.calls.some((command) => command.includes(' network ')), false, 'invalid port must not reach exec');

    await evaluate(cdp, sessionId, `document.getElementById('proxy-port').value = '12345'; document.getElementById('save-network').click()`);
    const networkCalls = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command.endsWith(' network 12345 --json')), 'network command');
    assert.ok(networkCalls.includes('/data/adb/modules/llc_zygisk/bin/llc-manager network 12345 --json'));
    await waitFor(cdp, sessionId,
      `(() => ({calls: window.__bridgeCalls.map(x => x.command), banner: document.getElementById('banner').textContent}))()`,
      (value) => value.calls.filter((command) => command.endsWith(' status --json')).length >= 5 && value.banner.includes('网络设置已保存'),
      'network status refresh');

    await evaluate(cdp, sessionId, `document.getElementById('cleanup-preview').click()`);
    const cleanupPreview = await waitFor(cdp, sessionId,
      `(() => ({calls: window.__bridgeCalls.map(x => x.command), state: document.getElementById('cleanup-state').textContent, item: document.getElementById('cleanup-list').textContent, hasImage: !!document.getElementById('cleanup-list').querySelector('img'), enabled: !document.getElementById('cleanup-apply').disabled, xss: window.__xss}))()`,
      (value) => value.calls.some((command) => command.endsWith(' cleanup-plan --json')) && value.enabled,
      'cleanup plan');
    assert.match(cleanupPreview.item, /<img src=x onerror=window\.__xss=1>/);
    assert.equal(cleanupPreview.hasImage, false, 'untrusted cleanup id must be rendered as text');
    assert.equal(cleanupPreview.xss, undefined, 'untrusted cleanup id must not execute');
    assert.match(cleanupPreview.state, /1 项/);

    await evaluate(cdp, sessionId, `document.getElementById('cleanup-apply').click()`);
    const cleanupPrompt = await waitFor(cdp, sessionId,
      `(() => ({open: document.getElementById('confirm-dialog').open, description: document.getElementById('confirm-description').textContent}))()`,
      (value) => value.open, 'cleanup confirmation dialog');
    assert.match(cleanupPrompt.description, /游戏资源包全部保留/);
    await evaluate(cdp, sessionId, `document.getElementById('confirm-accept').click()`);
    const cleanupCalls = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command === `/data/adb/modules/llc_zygisk/bin/llc-manager cleanup ${cleanupToken} --json`),
      'cleanup command with preview token');
    assert.ok(cleanupCalls.includes(`/data/adb/modules/llc_zygisk/bin/llc-manager cleanup ${cleanupToken} --json`));
    await waitFor(cdp, sessionId, `document.getElementById('banner').textContent`,
      (text) => text.includes('清理操作已完成'), 'cleanup completion');

    await evaluate(cdp, sessionId, `document.getElementById('font-select').value = 'TestFont.bundle'; document.getElementById('apply-font').click()`);
    const fontCalls = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command.endsWith(" set-font 'TestFont.bundle' --json")), 'font command');
    assert.ok(fontCalls.includes("/data/adb/modules/llc_zygisk/bin/llc-manager set-font 'TestFont.bundle' --json"));
    await waitFor(cdp, sessionId,
      `(() => ({banner: document.getElementById('banner').textContent, activeFont: document.getElementById('active-font').textContent, fontType: document.getElementById('font-type').textContent}))()`,
      (value) => value.banner.includes('中文字体配置已更新') && value.activeFont === 'TestFont.bundle' && value.fontType === '自定义 AssetBundle',
      'font status refresh');
  } finally {
    try { cdp?.close(); } catch {}
    if (chrome && chrome.exitCode === null) {
      if (process.platform === 'win32' && chrome.pid) {
        spawnSync('taskkill.exe', ['/PID', String(chrome.pid), '/T', '/F'], {windowsHide: true, stdio: 'ignore'});
      }
      try { chrome.kill(); } catch {}
    }
    if (chrome && chrome.exitCode === null) {
      await Promise.race([new Promise((resolve) => chrome.once('exit', resolve)), delay(2000)]);
    }
    if (serverListening) await new Promise((resolve) => server.close(resolve));
    const ownedPrefix = path.join(os.tmpdir(), 'llc-webui-browser-test-');
    if (temporaryRoot.startsWith(ownedPrefix)) {
      for (let attempt = 0; ; attempt++) {
        try { await rm(temporaryRoot, {recursive: true, force: true}); break; }
        catch (error) {
          if (attempt >= 9 || !['EBUSY', 'EPERM'].includes(error.code)) throw error;
          await delay(100);
        }
      }
    }
  }
});
