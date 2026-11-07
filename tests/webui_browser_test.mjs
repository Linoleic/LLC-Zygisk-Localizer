import assert from 'node:assert/strict';
import {spawn, spawnSync} from 'node:child_process';
import {mkdir, mkdtemp, readFile, rm, stat, writeFile} from 'node:fs/promises';
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
    const state = {staleRuntime: false, delays: {check: 0, progress: 0}, network: {mode: 'system', port: 7890, effective_source: 'system', mirror_mode: 'official', custom_mirror: ''}, activeFont: 'default', language: {mode: 'all', target_language: 'kr'}};
    const candidateKey = ${JSON.stringify(candidateKey)};
    const previousKey = ${JSON.stringify(previousKey)};
    const cleanupToken = ${JSON.stringify(cleanupToken)};
    const injectedMarkup = ${JSON.stringify(injectedMarkup)};
    window.__bridgeCalls = calls;
    window.__makeFontMissing = () => { state.fontMissing = true; state.activeFont = 'default'; };
    window.__makeRuntimeStale = () => { state.staleRuntime = true; };
    const makeStatus = () => ({
      kind: 'module_manager_status', schema: 1, busy: false, capabilities: {text_schema_audit: 1},
      text_schema: state.schemaChecked ? {state:'available',first_baseline:true,checked_at_unix:1793300000,game:{version:'1.118.1'},container_count:80,text_class_count:120,candidate_containers:[{name:injectedMarkup,type:'TextDataDictionary_New'}],missing_registered:[],delta_counts:{}} : {state:'not_checked'},
      game: {state: 'read', version: '1.118.0', version_code: 487},
      local_resources: {state: 'read', mode: 'active', version: '123', pack_key: 'disk-pack-key'},
      resource_store: {state: 'managed', active: 'disk-pack-key', previous: previousKey, network: state.network},
      network: {...state.network, https_transport: 'android_https'},
      fonts: {
        ready: !state.fontMissing,
        active_font: state.activeFont === 'default' ? '默认思源黑体' : state.activeFont,
        is_custom: state.activeFont !== 'default',
        available: [
          {name: '默认思源黑体', path: 'default', is_active: !state.fontMissing && state.activeFont === 'default', size_bytes: state.fontMissing ? 0 : 123456},
          {name: 'TestFont.bundle', path: '/sdcard/Android/data/com.ProjectMoon.LimbusCompany/files/fonts/TestFont.bundle', is_active: state.activeFont === 'TestFont.bundle', size_bytes: 123456}
        ]
      },
      language: {
        mode: state.language.mode,
        target_language: state.language.target_language,
        target_language_name: state.language.target_language === 'jp' ? '日语 (Japanese)' : state.language.target_language === 'en' ? '英语 (English)' : '韩语 (Korean)'
      },
      updates: {state: 'no_update', downloads: {state: 'not_started'}, last_success: {remote: {version: '123'}, checked_at_unix: 1793300000}},
      downloads: {state: 'not_started', prepared: state.fontMissing ? null : {key: candidateKey, files: 2, uncompressed_bytes: 2048, verified_at_unix: 1793300000, plan: {version: '124', size: 512, source: 'official_github_release_asset', sha256: 'a'.repeat(64)}}, last_attempt: null},
      runtime: {
        fresh_request: !state.staleRuntime,
        state: 'active',
        report: {
          kind: 'localization_status', pid: 24680, runtime: {stage: 'running'}, compatibility: {status: 'adaptive_approved'}, capabilities: {text_schema_audit:{protocol:1}},
          resources: {local_version: '123', pack_key: 'running-pack-key'},
          coverage: state.healthCoverage ?? {missing_local_rows: state.staleRuntime ? 7777 : 3, unsupported_source_fields: 1, state: 'module_coverage_limited'}
        }
      }
    });
    window.ksu = {exec(command, optionsJson, callbackName) {
      const call = {command, optionsJson, callbackName, startedAt: performance.now()};
      calls.push(call);
      let result;
      if (command.endsWith(' status --json') || command.endsWith(' check --json') || command.endsWith(" activate '${candidateKey}' --json") || command.endsWith(" rollback '${previousKey}' --json") || command.endsWith(' import-current --json')) {
        result = makeStatus();
      } else if (command.endsWith(' discover --json')) {
        state.schemaChecked = true; result = makeStatus();
      } else if (command.endsWith(' cleanup-plan --json')) {
        result = {kind: 'module_cleanup_plan', schema: 1, token: cleanupToken, bytes: 1024, items: [{id: injectedMarkup, bytes: 1024}]};
      } else if (command.endsWith(' cleanup ' + cleanupToken + ' --json')) {
        result = {kind: 'module_cleanup_result', schema: 1, phase: 'succeeded', deleted: [injectedMarkup]};
      } else if (command.endsWith(' download-font --json')) {
        state.fontMissing = false; state.activeFont = 'default';
        result = {kind: 'font_manager_summary', schema: 1, ready: true, active_font: '默认思源黑体', is_custom: false, available: [{name: '默认思源黑体', path: 'default', is_active: true, size_bytes: 123456}]};
      } else if (command.endsWith(" set-font 'TestFont.bundle' --json")) {
        state.activeFont = 'TestFont.bundle';
        result = makeStatus();
      } else if (command.endsWith(' set-language selective jp --json')) {
        state.language = {mode: 'selective', target_language: 'jp'};
        result = {schema: 1, kind: 'language_manager_summary', mode: 'selective', target_language: 'jp'};
      } else if (command.endsWith(' set-language all --json')) {
        state.language = {mode: 'all', target_language: 'kr'};
        result = makeStatus();
      } else if (command.includes(' network 12345')) {
        const mirrorMatch = command.match(/network \d+ ([a-z_]+)(?: '([^']+)')?/);
        state.network = {
          mode: 'loopback',
          port: 12345,
          effective_source: 'module_setting',
          mirror_mode: mirrorMatch ? mirrorMatch[1] : 'official',
          custom_mirror: mirrorMatch && mirrorMatch[2] ? mirrorMatch[2] : ''
        };
        result = makeStatus();
      } else if (command.endsWith(' reload --json')) {
        result = {kind: 'reload_manager_summary', schema: 1, game_running: true, game_pid: 24680, flag_created: true, message: '热重载信号已发送至游戏进程'};
      } else if (command.endsWith(' detect-proxy --json')) {
        result = {kind: 'proxy_detection_result', schema: 1, detected: true, recommended_port: 7890, app_name: 'Clash', package_name: 'com.follow.clash'};
      } else if (command.includes(' import-font ')) {
        state.activeFont = 'ImportedFont.bundle';
        result = makeStatus();
      } else if (command.endsWith(' progress --json')) {
        result = {kind: 'module_manager_progress', schema: 1, busy: false, downloads: {state: 'not_started'}};
      } else {
        result = {kind: 'module_manager_error', schema: 1, error: 'Unexpected test command'};
      }
      const delayMs = command.endsWith(' check --json') ? state.delays.check
        : command.endsWith(' progress --json') ? state.delays.progress : command.endsWith(' detect-proxy --json') ? (state.delays.detect ?? 5) : 5;
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
    ], {windowsHide: true, detached: process.platform !== 'win32', stdio: 'ignore'});
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
    const overview = await evaluate(cdp, sessionId, `({game: document.getElementById('game-version').textContent, build: document.getElementById('game-build').textContent, combined: ['remote-title', 'candidate-title'].every(id => document.getElementById(id).closest('[role=tabpanel]').id === 'panel-packages')})`);
    assert.equal(overview.game, '1.118.0');
    assert.equal(overview.build, '487');
    assert.equal(overview.combined, true);
    const packageInfo = await evaluate(cdp, sessionId, `({source: document.getElementById('candidate-source').textContent, digest: document.getElementById('candidate-digest').textContent, transport: document.getElementById('download-transport').textContent, activation: document.getElementById('candidate-activation').textContent})`);
    assert.equal(packageInfo.source, 'LLC 官方 GitHub 发布归档');
    assert.equal(packageInfo.digest, 'a'.repeat(64));
    assert.equal(packageInfo.transport, 'Android 系统 HTTPS');
    assert.match(packageInfo.activation, /等待启用/);
    assert.equal(initial.running, '123', 'fixture has equal disk and runtime versions');
    assert.match(initial.note, /重开游戏/);
    assert.match(initial.coverage, /待补译 3/);
    assert.match(initial.coverageNote, /部分内容暂时保留原文/);
    assert.equal(initial.width <= initial.innerWidth, true, `fixture layout overflows: ${initial.width}`);
    assert.equal(initial.calls.some((command) => command.includes(' check --json')), false, 'opening the page must not check remote updates');
    const disclosures = await evaluate(cdp, sessionId, `({settings: [...document.querySelectorAll('.settings-card')].every(x => !x.open), title: document.getElementById('next-title').textContent, next: document.getElementById('next-action').textContent})`);
    assert.equal(disclosures.settings, true);
    assert.equal(await evaluate(cdp, sessionId, `document.getElementById('panel-packages').hidden`), true, 'package controls have a separate tab');
    assert.equal(await evaluate(cdp, sessionId, `document.querySelectorAll('[role=tab]').length`), 4);
    assert.match(disclosures.title, /汉化包 124 已下载完成/);
    assert.match(disclosures.next, /启用/);
    if (process.env.LLC_WEBUI_ARTIFACT_DIR) {
      const directory = path.resolve(process.env.LLC_WEBUI_ARTIFACT_DIR); await mkdir(directory, {recursive: true});
      for (const [width, theme] of [[320, 'light'], [360, 'light'], [412, 'dark'], [800, 'light'], [1280, 'dark']]) {
        await cdp.send('Emulation.setDeviceMetricsOverride', {width, height: 840, deviceScaleFactor: 1, mobile: true}, sessionId);
        await cdp.send('Emulation.setEmulatedMedia', {features: [{name: 'prefers-color-scheme', value: theme}]}, sessionId);
        const overflow = await evaluate(cdp, sessionId, `(() => { let overflow=false; for(const tab of document.querySelectorAll('[role=tab]')) {tab.click();overflow ||= document.documentElement.scrollWidth > innerWidth;} document.getElementById('tab-overview').click();return overflow;})()`);
        assert.equal(overflow, false, `${width}px ${theme} layout must not overflow`);
        const shot = await cdp.send('Page.captureScreenshot', {format: 'png', captureBeyondViewport: false}, sessionId);
        await writeFile(path.join(directory, `overview-${width}-${theme}.png`), Buffer.from(shot.data, 'base64'));
      }
      await cdp.send('Emulation.setDeviceMetricsOverride', {width: 360, height: 800, deviceScaleFactor: 1, mobile: true}, sessionId);
      await cdp.send('Emulation.setEmulatedMedia', {features: []}, sessionId);
    }

    await evaluate(cdp, sessionId, `window.__bridgeState.delays.check = 2200; window.__bridgeState.delays.progress = 500; document.getElementById('check').click()`);
    const afterCheck = await waitFor(cdp, sessionId,
      `(() => ({calls: window.__bridgeCalls, banner: document.getElementById('banner').textContent}))()`,
      (value) => value.calls.some((call) => call.command.endsWith(' check --json')) && value.calls.some((call) => call.command.endsWith(' progress --json') && call.completedAt !== undefined) && value.banner.includes('查找已完成'),
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
    assert.match(stale.note, /进入游戏主页后刷新/);
    assert.equal(await evaluate(cdp, sessionId, `document.getElementById('trigger-reload').disabled`), true);

    await evaluate(cdp, sessionId, `document.getElementById('next-action').click()`);
    const activatePrompt = await waitFor(cdp, sessionId,
      `(() => ({open: document.getElementById('confirm-dialog').open, description: document.getElementById('confirm-description').textContent}))()`,
      (value) => value.open, 'activate confirmation dialog');
    assert.match(activatePrompt.description, /汉化包 124/);
    await evaluate(cdp, sessionId, `document.getElementById('confirm-accept').click()`);
    const afterActivate = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command.includes(`activate '${candidateKey}' --json`)), 'activate command');
    assert.ok(afterActivate.some((command) => command === `/data/adb/modules/llc_zygisk/bin/llc-manager activate '${candidateKey}' --json`));

    const readyAfterActivate = await waitFor(cdp, sessionId,
      `({calls: window.__bridgeCalls.map(x => x.command), ready: !document.getElementById('rollback').disabled, banner: document.getElementById('banner').textContent})`,
      (value) => value.ready && value.banner.includes('汉化包已切换'), 'activation completion');
    const beforeRollback = readyAfterActivate.calls.length;
    await evaluate(cdp, sessionId, `document.getElementById('rollback').click()`);
    const rollbackPrompt = await waitFor(cdp, sessionId,
      `(() => ({open: document.getElementById('confirm-dialog').open, description: document.getElementById('confirm-description').textContent}))()`,
      (value) => value.open, 'rollback confirmation dialog');
    assert.match(rollbackPrompt.description, /回退到上一个保留的汉化包/);
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
      (calls) => calls.some((command) => command.includes(' network 12345')), 'network command');
    assert.ok(networkCalls.some((c) => c.includes(' network 12345 official --json')));
    await waitFor(cdp, sessionId,
      `(() => ({calls: window.__bridgeCalls.map(x => x.command), banner: document.getElementById('banner').textContent}))()`,
      (value) => value.calls.filter((command) => command.endsWith(' status --json')).length >= 5 && value.banner.includes('网络设置已保存'),
      'network status refresh');

    await evaluate(cdp, sessionId, `
      document.getElementById('mirror-mode-select').value = 'custom';
      document.getElementById('mirror-mode-select').dispatchEvent(new Event('change'));
      document.getElementById('custom-mirror-url').value = 'https://mirror.custom.org/';
      document.getElementById('save-network').click();
    `);
    const customMirrorCalls = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command.includes(" network 12345 custom 'https://mirror.custom.org/'")), 'custom mirror command');
    assert.ok(customMirrorCalls.some((c) => c.includes(" network 12345 custom 'https://mirror.custom.org/' --json")));
    await waitFor(cdp, sessionId,
      `(() => ({calls: window.__bridgeCalls.map(x => x.command), banner: document.getElementById('banner').textContent}))()`,
      (value) => value.calls.filter((command) => command.endsWith(' status --json')).length >= 6 && value.banner.includes('网络设置已保存'),
      'custom mirror status refresh');

    const checkCountBeforeDetection = await evaluate(cdp, sessionId, `window.__bridgeCalls.filter(x => x.command.endsWith(' check --json')).length`);
    await evaluate(cdp, sessionId, `window.__bridgeState.delays.detect = 350; document.getElementById('detect-proxy').click(); document.getElementById('detect-proxy').click(); document.getElementById('check').click()`);
    const detectCalls = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command.endsWith(' detect-proxy --json')), 'detect-proxy command');
    assert.ok(detectCalls.some((c) => c.endsWith(' detect-proxy --json')));
    await waitFor(cdp, sessionId,
      `(() => ({port: document.getElementById('proxy-port').value, enabled: document.getElementById('proxy-enabled').checked, banner: document.getElementById('banner').textContent}))()`,
      (val) => val.port === '7890' && val.enabled && val.banner.includes('Clash'), 'detect-proxy auto-fill');
    const serialized = await evaluate(cdp, sessionId, `({detect: window.__bridgeCalls.filter(x => x.command.endsWith(' detect-proxy --json')).length, check: window.__bridgeCalls.filter(x => x.command.endsWith(' check --json')).length})`);
    assert.equal(serialized.detect, 1); assert.equal(serialized.check, checkCountBeforeDetection);

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

    await evaluate(cdp, sessionId, `
      document.getElementById('tab-settings').click();
      document.querySelector('#network-settings > summary').click();
      document.querySelector('#lang-settings > summary').click();
      document.getElementById('proxy-port').value = '23456'; document.getElementById('proxy-port').dispatchEvent(new Event('input'));
      document.getElementById('lang-mode-select').value = 'selective'; document.getElementById('lang-mode-select').dispatchEvent(new Event('change'));
      document.getElementById('target-lang-select').value = 'en'; document.getElementById('target-lang-select').dispatchEvent(new Event('change'));
      document.getElementById('refresh').click();
    `);
    await waitFor(cdp, sessionId, `!document.getElementById('refresh').disabled`, Boolean, 'draft refresh');
    const draft = await evaluate(cdp, sessionId, `({port: document.getElementById('proxy-port').value, lang: document.getElementById('target-lang-select').value, note: document.getElementById('network-draft-note').hidden})`);
    assert.equal(draft.port, '23456'); assert.equal(draft.lang, 'en'); assert.equal(draft.note, false);
    const acrossTabs = await evaluate(cdp, sessionId, `(() => {
      document.getElementById('tab-packages').click();
      const packagesVisible = !document.getElementById('panel-packages').hidden && document.getElementById('panel-overview').hidden;
      document.getElementById('tab-settings').click();
      return {packagesVisible, port: document.getElementById('proxy-port').value, lang: document.getElementById('target-lang-select').value,
        selected: [...document.querySelectorAll('[role=tab]')].filter(x => x.getAttribute('aria-selected') === 'true').length};
    })()`);
    assert.equal(acrossTabs.packagesVisible, true); assert.equal(acrossTabs.port, '23456'); assert.equal(acrossTabs.lang, 'en'); assert.equal(acrossTabs.selected, 1);
    const keyboardTab = await evaluate(cdp, sessionId, `(() => { const tab=document.getElementById('tab-settings'); tab.focus(); tab.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true})); return {focus: document.activeElement.id, visible: !document.getElementById('panel-diagnostics').hidden}; })()`);
    assert.equal(keyboardTab.focus, 'tab-diagnostics'); assert.equal(keyboardTab.visible, true);
    await evaluate(cdp, sessionId, `document.getElementById('tab-settings').click()`);

    await evaluate(cdp, sessionId, `document.getElementById('discard-network').click(); document.getElementById('discard-lang').click()`);
    const restored = await evaluate(cdp, sessionId, `({port: document.getElementById('proxy-port').value, mode: document.getElementById('lang-mode-select').value, note: document.getElementById('network-draft-note').hidden, disabled: document.getElementById('apply-lang').disabled})`);
    assert.equal(restored.port, '12345'); assert.equal(restored.mode, 'all'); assert.equal(restored.note, true); assert.equal(restored.disabled, true);

    await evaluate(cdp, sessionId, `document.getElementById('tab-settings').click(); document.querySelector('#font-settings > summary').click(); document.getElementById('font-select').value = 'TestFont.bundle'; document.getElementById('font-select').dispatchEvent(new Event('change')); document.getElementById('apply-font').click()`);
    const fontCalls = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command.endsWith(" set-font 'TestFont.bundle' --json")), 'font command');
    assert.ok(fontCalls.includes("/data/adb/modules/llc_zygisk/bin/llc-manager set-font 'TestFont.bundle' --json"));
    await waitFor(cdp, sessionId,
      `(() => ({banner: document.getElementById('banner').textContent, activeFont: document.getElementById('active-font').textContent, fontType: document.getElementById('font-type').textContent}))()`,
      (value) => value.banner.includes('中文字体配置已更新') && value.activeFont === 'TestFont.bundle' && value.fontType === '自定义字体包',
      'font status refresh');

    await evaluate(cdp, sessionId, `document.getElementById('lang-mode-select').value = 'selective'; document.getElementById('lang-mode-select').dispatchEvent(new Event('change')); document.getElementById('target-lang-select').value = 'jp'; document.getElementById('apply-lang').click()`);
    const langCalls = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command.endsWith(' set-language selective jp --json')), 'language command');
    assert.ok(langCalls.includes('/data/adb/modules/llc_zygisk/bin/llc-manager set-language selective jp --json'));
    await waitFor(cdp, sessionId,
      `(() => ({banner: document.getElementById('banner').textContent, langMode: document.getElementById('active-lang-mode').textContent, targetLang: document.getElementById('active-target-lang').textContent}))()`,
      (value) => value.banner.includes('语言设置已保存') && value.langMode === '仅替换特定语言' && value.targetLang === '日语 (Japanese)',
      'language status refresh');

    await evaluate(cdp, sessionId, `window.__bridgeState.staleRuntime = false; document.getElementById('refresh').click()`);
    await waitFor(cdp, sessionId, `!document.getElementById('trigger-reload').disabled`, Boolean, 'valid process enables reload');
    await evaluate(cdp, sessionId, `document.getElementById('trigger-reload').click()`);
    const reloadCalls = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command.endsWith(' reload --json')), 'reload command');
    assert.ok(reloadCalls.includes('/data/adb/modules/llc_zygisk/bin/llc-manager reload --json'));
    await waitFor(cdp, sessionId, `document.getElementById('banner').textContent`,
      (text) => text.includes('重新加载请求已发送'), 'reload banner');

    await evaluate(cdp, sessionId, `document.getElementById('custom-font-path').value = '/sdcard/Download/ImportedFont.bundle'; document.getElementById('import-custom-font').click()`);
    const importCalls = await waitFor(cdp, sessionId, `window.__bridgeCalls.map(x => x.command)`,
      (calls) => calls.some((command) => command.includes("import-font '/sdcard/Download/ImportedFont.bundle'")), 'import-font command');
    assert.ok(importCalls.some((c) => c.includes("import-font '/sdcard/Download/ImportedFont.bundle' --json")));
    await waitFor(cdp, sessionId, `!document.getElementById('refresh').disabled`, Boolean, 'last operation completed');
    await evaluate(cdp, sessionId, `document.querySelectorAll('details').forEach(x => { x.open = true; })`);
    for (const width of [320, 412, 800, 1280]) {
      await cdp.send('Emulation.setDeviceMetricsOverride', {width, height: 840, deviceScaleFactor: 1, mobile: true}, sessionId);
      assert.equal(await evaluate(cdp, sessionId, `document.documentElement.scrollWidth > innerWidth`), false, `expanded ${width}px layout must not overflow`);
    }
    assert.equal(await evaluate(cdp, sessionId, `document.getElementById('font-select-trigger').tabIndex >= 0 && document.getElementById('font-select-trigger').getAttribute('role') === 'combobox'`), true);
    await cdp.send('Emulation.setEmulatedMedia', {features: [{name: 'prefers-reduced-motion', value: 'no-preference'}]}, sessionId);
    const picker = await evaluate(cdp, sessionId, `(() => {
      const button = document.getElementById('mirror-mode-select-trigger'); button.scrollIntoView({block:'center'}); button.focus(); button.click();
      const menu = document.getElementById('mirror-mode-select-menu'), box = menu.getBoundingClientRect();
      const animated = menu.getAnimations().some(a => a.playState === 'running');
      button.click(); button.click();
      return {animated, expanded: button.getAttribute('aria-expanded'), fits: box.left >= 0 && box.right <= innerWidth && box.top >= 0 && box.bottom <= innerHeight};
    })()`);
    assert.equal(picker.animated, true); assert.equal(picker.expanded, 'true'); assert.equal(picker.fits, true);
    await delay(240);
    const afterPicker = await evaluate(cdp, sessionId, `({hidden:document.getElementById('mirror-mode-select-menu').hidden,expanded:document.getElementById('mirror-mode-select-trigger').getAttribute('aria-expanded')})`);
    assert.equal(afterPicker.hidden, false, 'rapid reversal: '+JSON.stringify(afterPicker));
    await cdp.send('Input.dispatchKeyEvent', {type: 'keyDown', key: 'Home'}, sessionId);
    await cdp.send('Input.dispatchKeyEvent', {type: 'keyDown', key: 'Enter'}, sessionId);
    assert.equal(await evaluate(cdp, sessionId, `document.getElementById('mirror-mode-select').value`), 'official');
    await evaluate(cdp, sessionId, `document.getElementById('discard-network').click()`);
    await delay(250);
    await evaluate(cdp, sessionId, `(() => { const s=document.getElementById('font-select'); for(let i=0;i<30;i++){const o=document.createElement('option');o.value='qa-font-'+i;o.textContent='Font '+i;s.append(o);} s.dispatchEvent(new Event('change')); const b=document.getElementById('font-select-trigger');b.scrollIntoView({block:'center'});b.focus();b.click();document.getElementById('font-select-menu').scrollTop=200; })()`);
    await delay(200);
    assert.equal(await evaluate(cdp, sessionId, `document.getElementById('font-select-trigger').getAttribute('aria-expanded')`), 'true', 'scrolling options must not close their popup');
    await cdp.send('Input.dispatchKeyEvent', {type: 'keyDown', key: 'Escape'}, sessionId);
    await delay(180);
    assert.equal(await evaluate(cdp, sessionId, `document.getElementById('font-select-menu').hidden`), true);
    const fullHeight = await evaluate(cdp, sessionId, `document.querySelector('#network-settings .disclosure-content').getBoundingClientRect().height`);
    await evaluate(cdp, sessionId, `document.querySelector('#network-settings > summary').click()`);
    await delay(70);
    const reversing = await evaluate(cdp, sessionId, `(() => { const body=document.querySelector('#network-settings .disclosure-content'), before=body.getBoundingClientRect().height;document.querySelector('#network-settings > summary').click();return {before,after:body.getBoundingClientRect().height,animated:body.getAnimations().length>0}; })()`);
    assert.ok(reversing.before > 0 && reversing.before < fullHeight);
    assert.ok(Math.abs(reversing.before - reversing.after) < 3, 'reverse from the rendered height without a jump');
    assert.equal(reversing.animated, true);
    await delay(260);
    assert.equal(await evaluate(cdp, sessionId, `document.getElementById('network-settings').open && document.querySelector('#network-settings .disclosure-content').style.height==='auto'`), true);
    await evaluate(cdp, sessionId, `document.querySelector('#network-settings > summary').click();document.querySelector('#network-settings > summary').click();document.querySelector('#network-settings > summary').click()`);
    await delay(260);
    assert.equal(await evaluate(cdp, sessionId, `document.getElementById('network-settings').open`), false);
    await cdp.send('Emulation.setEmulatedMedia', {features: [{name: 'prefers-reduced-motion', value: 'reduce'}]}, sessionId);
    assert.equal(await evaluate(cdp, sessionId, `(() => {document.querySelector('#network-settings > summary').click();return document.querySelector('#network-settings .disclosure-content').getAnimations().length;})()`), 0);
    await evaluate(cdp, sessionId, `window.__bridgeState.staleRuntime=false; window.__bridgeState.healthCoverage={state:'module_issue',structure_mismatches:1,missing_local_rows:38,observed_empty_tables:2,tables:[{table:'_gachaNotice',sync_state:'observed_empty',snapshot_current:true},{table:'<LobbyBGMCategory>k__BackingField',sync_state:'observed_empty',snapshot_current:true}]}; document.getElementById('tab-overview').click();document.getElementById('refresh').click()`);
    await waitFor(cdp, sessionId, `document.getElementById('health-detail').textContent`, text => text.includes('其他汉化继续生效'), 'live health warning');
    const healthWarning = await evaluate(cdp, sessionId, `({tone:document.getElementById('health-panel').dataset.tone,title:document.getElementById('health-title').textContent,compatibility:document.getElementById('health-compatibility').textContent,next:document.getElementById('next-action').textContent})`);
    assert.equal(healthWarning.tone,'warning');assert.match(healthWarning.title,/部分文本格式有变化/);assert.match(healthWarning.compatibility,/自动适配/);assert.match(healthWarning.next,/启用/);
    await evaluate(cdp, sessionId, `document.getElementById('view-health-details').click()`);
    assert.equal(await evaluate(cdp, sessionId, `!document.getElementById('panel-diagnostics').hidden && document.querySelector('.diagnostics-panel').open`),true);
    await evaluate(cdp, sessionId, `document.getElementById('discover-text').click()`);
    await waitFor(cdp, sessionId, `document.getElementById('schema-state').textContent`, text => text.includes('已记录'), 'schema baseline result');
    const schema = await evaluate(cdp, sessionId, `({calls:window.__bridgeCalls.filter(x=>x.command.endsWith(' discover --json')).length, text:document.getElementById('schema-items').textContent, image:!!document.querySelector('#schema-items img'), note:document.getElementById('schema-detail').textContent})`);
    assert.equal(schema.calls,1);assert.equal(schema.image,false);assert.match(schema.text, /<img/);assert.match(schema.note,/供下次检查时比较/);
    assert.match(schema.text,/抽取公告：目前没有相关文本/);assert.match(schema.text,/BGM 分类：目前没有相关文本/);assert.match(schema.text,/加载后会自动处理/);


    await evaluate(cdp, sessionId, `window.__makeFontMissing(); document.getElementById('refresh').click()`);
    await waitFor(cdp, sessionId, `document.getElementById('next-action').textContent`, text => text.includes('一键下载官方中文字体'), 'missing font guidance');
    const missingFont = await evaluate(cdp, sessionId, `({name:document.getElementById('active-font').textContent, disabled:document.querySelector('#font-select option[value="default"]').disabled})`);
    assert.equal(missingFont.name, '尚未准备中文字体');
    assert.equal(missingFont.disabled, true);
    await evaluate(cdp, sessionId, `document.getElementById('next-action').click()`);
    await waitFor(cdp, sessionId, `document.getElementById('banner').textContent`, text => text.includes('官方中文字体已下载并解压就绪'), 'font downloaded banner');
    assert.equal(await evaluate(cdp, sessionId, `document.getElementById('font-settings').open && document.getElementById('tab-settings').getAttribute('aria-selected') === 'true'`), true);

  } catch (error) {
    // Preserve the actual assertion if browser/profile cleanup also fails.
    console.error(error);
    throw error;
  } finally {
    try { await cdp?.send('Browser.close'); } catch {}
    try { cdp?.close(); } catch {}
    if (chrome && chrome.exitCode === null) {
      if (process.platform === 'win32' && chrome.pid) {
        spawnSync('taskkill.exe', ['/PID', String(chrome.pid), '/T', '/F'], {windowsHide: true, stdio: 'ignore'});
      }
      try { chrome.kill(); } catch {}
    }
    // Linux Chrome has child processes which may outlive its launcher. This
    // group belongs only to the detached process spawned by this test.
    if (process.platform !== 'win32' && chrome?.pid) {
      try { process.kill(-chrome.pid, 'SIGTERM'); } catch {}
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
          if (attempt >= 9 || !['EBUSY', 'EPERM', 'ENOTEMPTY'].includes(error.code)) throw error;
          await delay(100);
        }
      }
    }
  }
});
