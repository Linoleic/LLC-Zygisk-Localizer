import {available, exec} from './bridge.mjs';
import {command, parseResponse, model, nextStep, candidateDetails, downloadError, bytes, when, updateLabels, transferLabels} from './view-model.mjs?v=20261107download1';
import {setupSelects, setupDisclosures, setupTabs, showRegion} from './ui-controls.mjs?v=20261105schema1';

const el = id => document.getElementById(id);
const disclosures = setupDisclosures();
const pickers = setupSelects();
const tabs = setupTabs(() => pickers.close());
const actions = ['refresh', 'check', 'download', 'activate', 'rollback', 'import-current', 'save-network', 'detect-proxy', 'apply-font', 'import-custom-font', 'pick-font-file', 'download-font', 'apply-lang', 'trigger-reload', 'cleanup-preview', 'cleanup-apply', 'discover-text'];
let report = null, cleanup = null, pending = false, uncertain = false, polling = false, timer;
let pendingAction = null;
const drafts = new Set();
const actionButtons = {status: 'refresh', network: 'save-network', 'set-language': 'apply-lang', 'set-font': 'apply-font', 'download-font': 'download-font', reload: 'trigger-reload', 'detect-proxy': 'detect-proxy', discover: 'discover-text'};
const actionMessages = {discover: '正在检查文本适配，请稍候…', check: '正在检查汉化包更新…', download: '正在下载并校验汉化包…', 'download-font': '正在下载并解压官方思源黑体，请稍候…', activate: '正在启用已下载的汉化包…', rollback: '正在恢复上一个汉化包…', network: '正在保存网络设置…', 'set-font': '正在保存字体设置…', 'set-language': '正在保存语言策略…', reload: '正在发送重新加载请求…'};
const set = (id, value) => { el(id).textContent = value ?? '—'; };
function banner(message, tone = 'info') { set('banner', message); el('banner').dataset.tone = tone; }

function controls() {
  const blocked = pending || uncertain || report?.busy === true || !available();
  for (const id of actions) if (el(id)) el(id).disabled = blocked || !report;
  el('refresh').disabled = pending || !available();
  el('font-select').disabled = blocked || !report;
  el('lang-mode-select').disabled = blocked || !report;
  const isSelective = el('lang-mode-select').value === 'selective';
  el('target-lang-select').disabled = blocked || !report || !isSelective;
  if (el('target-lang-field')) {
    showRegion(el('target-lang-field'), isSelective);
  }
  const state = model(report);
  el('discover-text').disabled ||= !state.canDiscover;
  el('trigger-reload').disabled ||= !state.canReload;
  el('apply-lang').disabled ||= el('lang-mode-select').value === state.langMode && (state.langMode !== 'selective' || el('target-lang-select').value === state.targetLang);
  const activeFont = state.availableFonts.find(f => f.is_active);
  if (activeFont) el('apply-font').disabled ||= el('font-select').value === (activeFont.path === 'default' ? 'default' : activeFont.name);
  el('activate').disabled ||= !state.canActivate;
  el('rollback').disabled ||= !state.canRollback;
  el('import-current').disabled ||= !state.canImport;
  el('cleanup-apply').disabled ||= !cleanup?.items?.length;
  el('proxy-enabled').disabled = blocked;
  el('proxy-port').disabled = blocked || !el('proxy-enabled').checked;
  if (el('detect-proxy')) el('detect-proxy').disabled = blocked;
  if (el('mirror-mode-select')) el('mirror-mode-select').disabled = blocked;
  if (el('custom-mirror-url')) el('custom-mirror-url').disabled = blocked;
  for (const panel of ['network', 'lang']) {
    el(`${panel}-draft-note`).hidden = !drafts.has(panel);
    el(`discard-${panel}`).disabled = blocked;
  }
  el('apply-font').disabled ||= el('font-select').selectedOptions[0]?.disabled === true;
  const guide = nextStep(report);
  el('next-action').disabled = blocked || !guide.action || el(guide.action)?.disabled;
  const guideBusy = pending && (pendingAction === guide.action || (pendingAction === 'status' && guide.action === 'refresh'));
  el('next-action').classList.toggle('is-loading', guideBusy); el('next-action').setAttribute('aria-busy', String(guideBusy));
  document.querySelectorAll('#importable-fonts-list button').forEach(button => { button.disabled = blocked || !report; });
  for (const id of actions) {
    const button = el(id); if (!button) continue;
    const loading = pending && id === (actionButtons[pendingAction] ?? pendingAction);
    button.classList.toggle('is-loading', loading); button.setAttribute('aria-busy', String(loading));
  }
  pickers.sync();
}
function transfer(downloads, busy) {
  if (!downloads) return;
  const candidate = downloads.prepared, attempt = downloads.last_attempt;
  set('candidate-version', candidate?.plan?.version ?? '尚未下载');
  el('candidate-files').replaceChildren();
  const item = document.createElement('li');
  item.textContent = candidate ? `${candidate.files} 个文件 · 解压后 ${bytes(candidate.uncompressed_bytes)} · ${candidate.key}` : '下载并通过校验后显示';
  el('candidate-files').append(item);
  const phase = downloads.state ?? (attempt ? ['failed', 'succeeded'].includes(attempt.phase) ? attempt.phase : busy ? attempt.phase : 'operation_interrupted' : 'not_started');
  set('transfer-state', `${transferLabels[phase] ?? phase}${attempt?.operation ? ` · ${attempt.operation}` : ''}${attempt?.error ? ` · ${downloadError(attempt.error)}` : ''}`);
  const total = attempt?.plan?.size, received = attempt?.bytes_received;
  if (Number.isFinite(received) && total > 0) el('transfer-progress').value = Math.min(100, received / total * 100);
  else if (phase === 'downloading' || phase === 'validating' || phase === 'working') el('transfer-progress').removeAttribute('value');
  else el('transfer-progress').value = phase === 'succeeded' ? 100 : 0;
  set('transfer-bytes', total > 0 ? `${bytes(received ?? 0)} / ${bytes(total)}` : '—');
}
function render(doc) {
  report = doc;
  const details = candidateDetails(doc);
  for (const [id, value] of Object.entries({'candidate-source': details.source, 'candidate-archive': details.archive,
    'candidate-verified': details.verified, 'candidate-digest': details.digest, 'candidate-activation': details.activation,
    'download-transport': details.transport, 'download-check-error': details.checkError})) set(id, value);
  const state = model(doc), updates = doc.updates ?? {}, network = doc.network ?? {};
  if (el('reboot-warning')) el('reboot-warning').hidden = !state.rebootRequired;
  set('health-title', state.health.title); set('health-detail', state.health.detail);
  set('health-compatibility', state.health.compatibility);
  el('health-panel').dataset.tone = state.health.tone;
  el('health-compatibility').hidden = !state.health.compatibility;
  el('view-health-details').hidden = !state.health.canInspect;
  const guide = nextStep(doc);
  set('schema-state', state.schemaAudit.title); set('schema-detail', state.schemaAudit.detail);
  el('schema-items').replaceChildren();
  for (const line of state.schemaAudit.items) { const item = document.createElement('li'); item.textContent = line; el('schema-items').append(item); }
  el('network-help').hidden = !(['check_failed', 'check_interrupted'].includes(updates.state) || (doc.downloads?.last_attempt?.operation === 'download' && doc.downloads.last_attempt.phase === 'failed'));
  set('next-title', guide.title); set('next-detail', guide.detail); set('next-action', guide.label);
  for (const [id, value] of Object.entries({'disk-version': state.diskVersion, 'disk-mode': state.diskMode,
    'game-version': state.gameVersion, 'game-build': state.gameBuild,
    'module-version': state.moduleVersion, 'manager-build': state.managerBuild, 'core-build': state.coreBuild,
    'running-version': state.runningVersion, 'running-state': state.runningState, 'switch-note': state.switchNote,
    coverage: state.coverage, 'coverage-note': state.coverageNote, 'remote-version': updates.last_success?.remote?.version ?? '尚未取得',
    'update-state': updateLabels[updates.state] ?? updates.state, 'checked-at': when(updates.last_success?.checked_at_unix),
    'active-font': state.activeFont, 'font-type': state.fontType,
    'active-lang-mode': state.langModeDisplay, 'active-target-lang': state.targetLangDisplay,
    'detected-game-lang': state.detectedLangDisplay})) set(id, value);
  transfer(doc.downloads, doc.busy);

  if (!cleanup && doc.cleanup) set('cleanup-state', `上次清理：${({succeeded: '成功', failed: '失败', interrupted: '中断，请重新预览', working: '进行中'})[doc.cleanup.phase] ?? '未知'}；已处理 ${doc.cleanup.deleted?.length ?? 0} 项。${doc.cleanup.error ?? ''}`);
  if (Array.isArray(state.availableFonts) && el('font-select')) {
    const sel = el('font-select');
    const curVal = drafts.has('font') ? sel.value : null;
    sel.replaceChildren();
    for (const f of state.availableFonts) {
      const opt = document.createElement('option');
      opt.value = f.path === 'default' ? 'default' : f.name;
      opt.disabled = f.size_bytes === 0;
      opt.textContent = `${f.name}${f.size_bytes > 0 ? ` (${bytes(f.size_bytes)})` : ''}${f.size_bytes === 0 ? ' [未准备]' : f.is_active ? ' [已选择]' : ''}`;
      if (f.is_active) opt.selected = true;
      sel.append(opt);
    }
    if (curVal && Array.from(sel.options).some(o => o.value === curVal)) {
      sel.value = curVal;
    }
  }
  if (el('importable-fonts-list')) {
    const list = el('importable-fonts-list');
    const emptyNote = el('importable-empty-note');
    list.replaceChildren();
    if (state.importableFonts && state.importableFonts.length > 0) {
      if (emptyNote) emptyNote.style.display = 'none';
      for (const imp of state.importableFonts) {
        const li = document.createElement('li');
        li.className = 'importable-item';
        const info = document.createElement('div');
        info.className = 'importable-info';
        const name = document.createElement('span');
        name.className = 'importable-name';
        name.textContent = imp.name;
        const meta = document.createElement('span');
        meta.className = 'importable-meta';
        meta.textContent = `${bytes(imp.size_bytes)} · ${imp.path}`;
        info.append(name, meta);
        const btn = document.createElement('button');
        btn.className = 'button button-secondary';
        btn.type = 'button';
        btn.innerHTML = `<svg class="btn-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path><polyline points="7 10 12 15 17 10"></polyline><line x1="12" y1="15" x2="12" y2="3"></line></svg><span>导入</span>`;
        btn.addEventListener('click', () => run('import-font', imp.path));
        li.append(info, btn);
        list.append(li);
      }
    } else {
      if (emptyNote) emptyNote.style.display = '';
    }
  }
  if (!drafts.has('lang')) {
    el('lang-mode-select').value = state.langMode;
    el('target-lang-select').value = state.targetLang;
  }
  if (!drafts.has('network')) {
    el('mirror-mode-select').value = network.mirror_mode ?? 'official';
    el('custom-mirror-url').value = network.custom_mirror ?? '';
    el('proxy-enabled').checked = network.mode === 'loopback'; el('proxy-port').value = network.port ?? 7890;
  }
  updateMirrorFields();
  const mirrorNames = {
    official: '官方 GitHub 直连',
    ghproxy: 'ghproxy.net 镜像加速',
    ghfast: 'ghfast.top 镜像加速',
    custom: `自建 CDN (${network.custom_mirror || '未配置'})`
  };
  const mirrorText = mirrorNames[network.mirror_mode] ?? network.mirror_mode ?? '官方 GitHub 直连';
  const routeText = network.effective_source === 'caller_environment' ? '继承宿主代理环境' : network.mode === 'loopback' ? `本地代理 127.0.0.1:${network.port}` : '系统直连';
  set('network-note', `当前下载加速源：${mirrorText} ｜ 网络路由：${routeText}`);
  set('diagnostics', JSON.stringify(doc, null, 2)); controls();
  requestAnimationFrame(() => document.body.classList.remove('preload'));
}
function updateMirrorFields() {
  const mode = el('mirror-mode-select')?.value;
  const customField = el('custom-mirror-field');
  if (customField) {
    showRegion(customField, mode === 'custom');
  }
}
async function invoke(action, ...args) {
  const result = await exec(command(action, ...args), {timeoutMs: ['download', 'download-font'].includes(action) ? 300000 : 60000});
  const doc = parseResponse(result);
  if (doc.kind === 'module_manager_error') throw new Error(doc.error ?? '管理器执行失败');
  return {doc, errno: result.errno};
}
function schedule() {
  clearTimeout(timer);
  if ((pending || uncertain || report?.busy) && !document.hidden) timer = setTimeout(poll, 2000);
}
async function poll() {
  if (polling || document.hidden) return;
  const recovery = !pending;
  polling = true;
  try {
    const {doc} = await invoke('progress');
    if (doc.kind !== 'module_manager_progress') throw new Error('进度报告格式不匹配');
    if (!pending && !uncertain && !report?.busy) return;
    transfer(doc.downloads, doc.busy);
    if (recovery && !pending && !doc.busy && (uncertain || report?.busy)) {
      uncertain = false;
      await run('status');
      return;
    }
  } catch (error) { banner(`进度读取失败：${error.message}。可刷新状态重试；原操作可能仍在运行。`); }
  finally { polling = false; controls(); schedule(); }
}
function showCleanup(doc) {
  cleanup = doc; el('cleanup-list').replaceChildren();
  for (const item of doc.items ?? []) { const li = document.createElement('li'); li.textContent = `${item.id} · ${bytes(item.bytes)}`; el('cleanup-list').append(li); }
  set('cleanup-state', `可清理 ${doc.items?.length ?? 0} 项，共 ${bytes(doc.bytes)}。现有汉化包、字体和已下载的汉化包会保留。`);
}
async function run(action, ...args) {
  if (pending) return;
  pending = true; pendingAction = action; controls(); banner(action === 'status' ? '正在读取资源与运行状态…' : actionMessages[action] ?? '正在执行操作，请稍候…'); schedule();
  let message, tone = 'info';
  try {
    const {doc, errno} = await invoke(action, ...args);
    if (errno === 0 && doc.phase !== 'failed') {
      if (action === 'network') drafts.delete('network');
      if (action === 'set-language') drafts.delete('lang');
      if (['set-font', 'import-font'].includes(action)) drafts.delete('font');
    }
    if (doc.kind === 'module_manager_status') render(doc);
    else if (doc.kind === 'module_cleanup_plan') showCleanup(doc);
    else if (!['module_cleanup_result', 'reload_manager_summary', 'font_manager_summary', 'language_manager_summary'].includes(doc.kind)) throw new Error('返回类型与操作不匹配');
    if (action === 'cleanup') { cleanup = null; set('cleanup-state', `清理结果：${doc.phase}；已处理 ${doc.deleted?.length ?? 0} 项。${doc.error ?? ''}`); }
    if (!['status', 'cleanup-plan'].includes(action)) {
      if (!['cleanup', 'discover'].includes(action)) { cleanup = null; set('cleanup-state', '资源状态已改变，请重新预览。'); }
      const refreshed = await invoke('status'); render(refreshed.doc);
    }
    uncertain = false;
    const failed = errno !== 0 || doc.phase === 'failed' || (action === 'reload' && doc.flag_created !== true);
    tone = failed ? 'error' : action === 'status' ? 'info' : 'success';
    const errorCode = doc.error ?? (action === 'check' ? doc.updates?.last_attempt?.error : doc.downloads?.last_attempt?.error);
    const reason = downloadError(errorCode) || '请查看诊断信息';
    message = failed ? `本次操作未成功：${errno === 3 ? '另一个管理操作正在进行' : reason}。` : action === 'status' ? '状态已刷新，请按下方提示继续。' : action === 'discover' ? '文本适配检查完成，可在诊断页查看结果。' : action === 'check' ? '查找已完成，请按下方提示继续下载或使用现有汉化。' : action === 'download' ? '汉化包已下载并通过校验，下一步请确认启用。' : action === 'import-current' ? '现有汉化已保留，现在可以继续启用已下载的汉化包。' : ['activate', 'rollback'].includes(action) ? '汉化包已切换。请完全退出并重新打开游戏，无需重启设备。' : action === 'network' ? '本模块网络设置已保存。' : action === 'set-font' ? '中文字体配置已更新，将在完全退出并重开游戏后生效。' : action === 'import-font' ? `字体 ${doc.imported_font ?? ''} 已导入并选中，重新打开游戏后加载。` : action === 'download-font' ? '官方中文字体已下载并解压就绪，重新打开游戏后加载。' : action === 'set-language' ? '语言设置已保存。若游戏内尚未变化，请重新打开游戏。' : action === 'reload' ? '重新加载请求已发送；请刷新运行状态确认完成情况。' : action === 'cleanup-plan' ? '清理预览已生成，请核对项目。' : '清理操作已完成。';
  } catch (error) {
    tone = 'error';
    uncertain = error.code === 'bridge_timeout' || error.code === 'bridge_invalid_response' || error.code === 'bridge_output_too_large';
    message = `${error.message}${uncertain ? '。命令可能仍在运行，正在读取进度，暂不重复执行。' : ''}`;
  } finally { pending = false; pendingAction = null; banner(message, tone); controls(); schedule(); }
}
async function confirm(title, description) {
  const dialog = el('confirm-dialog'); set('confirm-title', title); set('confirm-description', description);
  return new Promise(resolve => {
    let accepted = false;
    el('confirm-accept').onclick = () => { accepted = true; dialog.close(); };
    el('confirm-cancel').onclick = () => dialog.close();
    dialog.addEventListener('close', () => { el('confirm-accept').onclick = null; el('confirm-cancel').onclick = null; resolve(accepted); }, {once: true});
    dialog.showModal();
  });
}
for (const id of ['refresh', 'check', 'download']) el(id).addEventListener('click', () => run(id === 'refresh' ? 'status' : id));
el('discover-text').addEventListener('click', () => run('discover'));
el('view-health-details').addEventListener('click', () => { tabs.select('diagnostics'); disclosures.open(document.querySelector('.diagnostics-panel')); document.querySelector('.diagnostics-panel').scrollIntoView({block: 'start'}); });
el('network-help').addEventListener('click', () => { tabs.select('settings'); disclosures.open(el('network-settings')); el('network-settings').scrollIntoView({block: 'start', behavior: 'smooth'}); });

el('next-action').addEventListener('click', () => {
  const target = el(nextStep(report).action); if (!target || target.disabled) return;
  // Keep guidance optional; all operations are also available on their own tab.
  if (target.id === 'download') tabs.select('packages');
  if (target.id === 'pick-font-file' || target.id === 'download-font') { tabs.select('settings'); disclosures.open(el('font-settings')); }
  target.click();
});
for (const [panel, fields] of Object.entries({network: ['proxy-enabled', 'proxy-port', 'mirror-mode-select', 'custom-mirror-url'], lang: ['lang-mode-select', 'target-lang-select'], font: ['font-select']})) {
  for (const id of fields) for (const event of ['input', 'change']) el(id).addEventListener(event, () => { drafts.add(panel); controls(); });
  if (panel !== 'font') el(`discard-${panel}`).addEventListener('click', () => { drafts.delete(panel); if (report) render(report); });
}
for (const action of ['activate', 'rollback', 'import-current']) el(action).addEventListener('click', async () => {
  const target = action === 'activate' ? report?.downloads?.prepared?.key : action === 'rollback' ? report?.resource_store?.previous : undefined;
  const title = ({activate: '启用已下载的汉化包', rollback: '恢复上一个汉化包', 'import-current': '保存现有汉化'})[action];
  const description = action === 'activate' ? `将启用汉化包 ${report?.downloads?.prepared?.plan?.version ?? '版本未知'}。当前游戏继续使用原包，完全退出再打开后生效。` : action === 'rollback' ? '回退到上一个保留的汉化包。当前游戏继续使用原包，完全退出再打开后生效。' : `将现有资源 ${report?.local_resources?.version ?? ''} 保存为可恢复的旧包，完成后即可继续启用新版。`;
  if (await confirm(title, description)) await run(action, target);
});
el('save-network').addEventListener('click', () => {
  const port = el('proxy-port').value;
  const proxyTarget = el('proxy-enabled').checked ? port : 'system';
  const mirrorMode = el('mirror-mode-select')?.value || 'official';
  const customMirror = el('custom-mirror-url')?.value?.trim() || '';
  if (mirrorMode === 'custom' && (!customMirror.startsWith('https://') || customMirror.length < 10)) {
    banner('自定义 CDN 节点前缀必须是以 https:// 开头的合法网址。');
    return;
  }
  try { command('network', proxyTarget, mirrorMode, customMirror); }
  catch { banner('网络设置参数无效，请输入 1–65535 端口与合规网址。'); return; }
  run('network', proxyTarget, mirrorMode, customMirror);
});
el('proxy-enabled').addEventListener('change', controls);
el('mirror-mode-select')?.addEventListener('change', updateMirrorFields);
el('detect-proxy')?.addEventListener('click', async () => {
  if (pending || uncertain || report?.busy) return;
  pending = true; pendingAction = 'detect-proxy'; controls();
  banner('正在扫描正在运行的本地代理软件及监听端口…');
  try {
    const {doc} = await invoke('detect-proxy');
    if (doc.detected && doc.recommended_port) {
      el('proxy-port').value = doc.recommended_port;
      el('proxy-enabled').checked = true;
      drafts.add('network');
      controls();
      const appName = doc.app_name || doc.package_name || '代理客户端';
      banner(`已检测到 ${appName} (端口 ${doc.recommended_port})，已自动填入。点击“保存网络与加速设置”生效。`);
    } else {
      banner('未检测到正在运行的本地代理客户端。请确认代理软件已启动，或手动输入端口。');
    }
  } catch (error) {
    banner(`代理检测失败：${error.message}`);
  } finally { pending = false; pendingAction = null; controls(); }
});
el('apply-font').addEventListener('click', () => {
  const target = el('font-select')?.value;
  if (target) run('set-font', target);
});
el('download-font')?.addEventListener('click', () => run('download-font'));
el('import-custom-font')?.addEventListener('click', () => {
  const p = el('custom-font-path')?.value?.trim();
  if (p) run('import-font', p);
});

function arrayBufferToBase64(buffer) {
  let binary = '';
  const bytesArr = new Uint8Array(buffer);
  const len = bytesArr.byteLength;
  for (let i = 0; i < len; i++) {
    binary += String.fromCharCode(bytesArr[i]);
  }
  return btoa(binary);
}

el('pick-font-file')?.addEventListener('click', () => {
  el('font-file-picker')?.click();
});

el('font-file-picker')?.addEventListener('change', async (event) => {
  const file = event.target.files?.[0];
  if (!file) return;
  event.target.value = '';
  if (!file.name.endsWith('.bundle') && !file.name.endsWith('.ab') && !file.name.endsWith('.unity3d')) {
    banner('所选文件非 .bundle 格式，请选取 Unity TMP AssetBundle 字体包。');
    return;
  }

  pending = true; controls(); banner(`正在准备导入字体 ${file.name} (${bytes(file.size)})…`); schedule();
  try {
    let resolved = false;
    try {
      const {doc} = await invoke('import-font', file.name);
      if (doc.kind === 'font_manager_summary') {
        resolved = true;
        banner(`字体 ${file.name} 已导入并选中，重新打开游戏后加载。`);
        const refreshed = await invoke('status'); render(refreshed.doc);
      }
    } catch {
      resolved = false;
    }

    if (!resolved) {
      banner(`正在从系统文件管理器流式读取并传输 ${file.name}…`);
      const token = Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
      const chunkSize = 128 * 1024;
      let offset = 0;
      let chunkIndex = 0;
      const totalBytes = file.size;

      while (offset < totalBytes) {
        const slice = file.slice(offset, offset + chunkSize);
        const buffer = await slice.arrayBuffer();
        const b64 = arrayBufferToBase64(buffer);
        await invoke('upload-chunk', `${token}:${chunkIndex === 0 ? 1 : 0}`, b64);
        offset += slice.size;
        chunkIndex++;
        const progress = Math.min(100, Math.round((offset / totalBytes) * 100));
        banner(`正在上传字体文件：${progress}% (${bytes(offset)} / ${bytes(totalBytes)})…`);
      }

      banner(`正在校验 AssetBundle 格式并安装字体 ${file.name}…`);
      const importResult = await invoke('import-font', `/data/local/tmp/llc_font_${token}.tmp`, file.name);
      if (importResult.doc.kind === 'font_manager_summary') {
        drafts.delete('font');
        banner(`字体 ${file.name} 已导入并选中，重新打开游戏后加载。`);
        const refreshed = await invoke('status'); render(refreshed.doc);
      } else {
        banner(`导入字体失败：${importResult.doc.error ?? '格式不符合要求'}`);
      }
    }
  } catch (err) {
    banner(`选取字体导入失败：${err.message}`);
  } finally {
    pending = false; controls(); schedule();
  }
});
el('trigger-reload')?.addEventListener('click', () => run('reload'));
el('apply-lang').addEventListener('click', () => {
  const mode = el('lang-mode-select')?.value;
  const target = el('target-lang-select')?.value;
  run('set-language', mode === 'selective' ? `selective:${target}` : 'all');
});
el('lang-mode-select').addEventListener('change', controls);
el('cleanup-preview').addEventListener('click', () => run('cleanup-plan'));
el('cleanup-apply').addEventListener('click', async () => {
  const preview = cleanup;
  if (preview?.items?.length && await confirm('清理暂存文件', `删除以下 ${preview.items.length} 项，共 ${bytes(preview.bytes)}：\n${preview.items.map(x => x.id).join('\n')}\n游戏资源包全部保留。`)) await run('cleanup', preview.token);
});
document.addEventListener('visibilitychange', schedule);
controls();
if (available()) run('status');
else {
  set('next-title', '请从模块管理器打开'); set('next-detail', '需要 Root 管理器提供操作通路。');
  banner('需要从支持 KernelSU WebUI 的 Root 管理器打开本模块页面。当前环境没有 Root 桥接，操作已禁用。');
}
