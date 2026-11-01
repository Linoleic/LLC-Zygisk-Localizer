import {available, exec} from './bridge.mjs';
import {command, parseResponse, model, bytes, when, updateLabels, transferLabels} from './view-model.mjs';

const el = id => document.getElementById(id);
const actions = ['refresh', 'check', 'download', 'activate', 'rollback', 'import-current', 'save-network', 'apply-font', 'apply-lang', 'cleanup-preview', 'cleanup-apply'];
let report = null, cleanup = null, pending = false, uncertain = false, polling = false, timer;
const set = (id, value) => { el(id).textContent = value ?? '—'; };
function banner(message) { set('banner', message); }
function controls() {
  const blocked = pending || uncertain || report?.busy === true || !available();
  for (const id of actions) el(id).disabled = blocked || !report;
  el('refresh').disabled = pending || !available();
  el('font-select').disabled = blocked || !report;
  el('lang-mode-select').disabled = blocked || !report;
  el('target-lang-select').disabled = blocked || !report || el('lang-mode-select').value !== 'selective';
  const state = model(report);
  el('activate').disabled ||= !state.canActivate;
  el('rollback').disabled ||= !state.canRollback;
  el('import-current').disabled ||= !state.canImport;
  el('cleanup-apply').disabled ||= !cleanup?.items?.length;
  el('proxy-enabled').disabled = blocked;
  el('proxy-port').disabled = blocked || !el('proxy-enabled').checked;
}
function transfer(downloads, busy) {
  if (!downloads) return;
  const candidate = downloads.prepared, attempt = downloads.last_attempt;
  set('candidate-version', candidate?.plan?.version ?? '尚无已校验候选包');
  el('candidate-files').replaceChildren();
  const item = document.createElement('li');
  item.textContent = candidate ? `${candidate.files} 个文件 · 解压后 ${bytes(candidate.uncompressed_bytes)} · ${candidate.key}` : '下载并通过校验后显示';
  el('candidate-files').append(item);
  const phase = downloads.state ?? (attempt ? ['failed', 'succeeded'].includes(attempt.phase) ? attempt.phase : busy ? attempt.phase : 'operation_interrupted' : 'not_started');
  set('transfer-state', `${transferLabels[phase] ?? phase}${attempt?.operation ? ` · ${attempt.operation}` : ''}${attempt?.error ? ` · ${attempt.error}` : ''}`);
  const total = attempt?.plan?.size, received = attempt?.bytes_received;
  if (Number.isFinite(received) && total > 0) el('transfer-progress').value = Math.min(100, received / total * 100);
  else if (phase === 'downloading' || phase === 'validating' || phase === 'working') el('transfer-progress').removeAttribute('value');
  else el('transfer-progress').value = phase === 'succeeded' ? 100 : 0;
  set('transfer-bytes', total > 0 ? `${bytes(received ?? 0)} / ${bytes(total)}` : '—');
}
function render(doc) {
  report = doc;
  const state = model(doc), updates = doc.updates ?? {}, network = doc.network ?? {};
  for (const [id, value] of Object.entries({'disk-version': state.diskVersion, 'disk-mode': state.diskMode,
    'running-version': state.runningVersion, 'running-state': state.runningState, 'switch-note': state.switchNote,
    coverage: state.coverage, 'coverage-note': state.coverageNote, 'remote-version': updates.last_success?.remote?.version ?? '尚未取得',
    'update-state': updateLabels[updates.state] ?? updates.state, 'checked-at': when(updates.last_success?.checked_at_unix),
    'active-font': state.activeFont, 'font-type': state.fontType,
    'active-lang-mode': state.langModeDisplay, 'active-target-lang': state.targetLangDisplay})) set(id, value);
  transfer(doc.downloads, doc.busy);
  if (!cleanup && doc.cleanup) set('cleanup-state', `上次清理：${({succeeded: '成功', failed: '失败', interrupted: '中断，请重新预览', working: '进行中'})[doc.cleanup.phase] ?? '未知'}；已处理 ${doc.cleanup.deleted?.length ?? 0} 项。${doc.cleanup.error ?? ''}`);
  if (Array.isArray(state.availableFonts) && el('font-select')) {
    const sel = el('font-select');
    const curVal = sel.value;
    sel.replaceChildren();
    for (const f of state.availableFonts) {
      const opt = document.createElement('option');
      opt.value = f.path === 'default' ? 'default' : f.name;
      opt.textContent = `${f.name}${f.size_bytes > 0 ? ` (${bytes(f.size_bytes)})` : ''}${f.is_active ? ' [当前生效]' : ''}`;
      if (f.is_active) opt.selected = true;
      sel.append(opt);
    }
    if (curVal && Array.from(sel.options).some(o => o.value === curVal)) {
      sel.value = curVal;
    }
  }
  if (el('lang-mode-select') && state.langMode) el('lang-mode-select').value = state.langMode;
  if (el('target-lang-select') && state.targetLang) el('target-lang-select').value = state.targetLang;
  el('proxy-enabled').checked = network.mode === 'loopback'; el('proxy-port').value = network.port ?? 7890;
  set('network-note', network.effective_source === 'caller_environment' ? '本次调用使用调用方代理环境；已保存的模块设置不会覆盖它。' : network.mode === 'loopback' ? `仅本模块使用 127.0.0.1:${network.port}，请确保设备代理正在监听。` : '使用当前系统网络环境。');
  set('diagnostics', JSON.stringify(doc, null, 2)); controls();
}
async function invoke(action, operand) {
  const result = await exec(command(action, operand), {timeoutMs: action === 'download' ? 300000 : 60000});
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
  set('cleanup-state', `可清理 ${doc.items?.length ?? 0} 项，共 ${bytes(doc.bytes)}。所有游戏资源包、当前候选包和对应压缩包均保留。`);
}
async function run(action, operand) {
  if (pending) return;
  pending = true; controls(); banner(action === 'status' ? '正在读取磁盘和当前进程状态…' : '正在执行操作，请稍候…'); schedule();
  let message;
  try {
    const {doc, errno} = await invoke(action, operand);
    if (doc.kind === 'module_manager_status') render(doc);
    else if (doc.kind === 'module_cleanup_plan') showCleanup(doc);
    else if (doc.kind !== 'module_cleanup_result') throw new Error('返回类型与操作不匹配');
    if (action === 'cleanup') { cleanup = null; set('cleanup-state', `清理结果：${doc.phase}；已处理 ${doc.deleted?.length ?? 0} 项。${doc.error ?? ''}`); }
    if (!['status', 'cleanup-plan'].includes(action)) {
      if (action !== 'cleanup') { cleanup = null; set('cleanup-state', '资源状态已改变，请重新预览。'); }
      const refreshed = await invoke('status'); render(refreshed.doc);
    }
    uncertain = false;
    const failed = errno !== 0 || doc.phase === 'failed';
    const reason = doc.error ?? (action === 'check' ? doc.updates?.last_attempt?.error : doc.downloads?.last_attempt?.error) ?? '请查看诊断信息';
    message = failed ? `本次操作未成功：${errno === 3 ? '另一个管理操作正在进行' : reason}。` : action === 'status' ? '状态已刷新。打开页面不会自动检查网络更新。' : action === 'check' ? '更新检查已完成。请查看版本比较结果；版本相同不代表全部文本已翻译。' : action === 'download' ? '候选包已下载并通过校验，尚未激活。' : ['activate', 'rollback', 'import-current'].includes(action) ? '磁盘资源已提交；运行中的游戏继续使用原包，下次冷启动生效。' : action === 'network' ? '本模块网络设置已保存。' : action === 'set-font' ? '中文字体配置已更新，将在下次冷启动游戏时生效。' : action === 'set-language' ? '多语言共存配置已更新，将在游戏中即时或下次冷启动生效。' : action === 'cleanup-plan' ? '清理预览已生成，请核对项目。' : '清理操作已完成。';
  } catch (error) {
    uncertain = error.code === 'bridge_timeout' || error.code === 'bridge_invalid_response' || error.code === 'bridge_output_too_large';
    message = `${error.message}${uncertain ? '。命令可能仍在运行，正在读取进度，暂不重复执行。' : ''}`;
  } finally { pending = false; banner(message); controls(); schedule(); }
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
for (const action of ['activate', 'rollback', 'import-current']) el(action).addEventListener('click', async () => {
  const target = action === 'activate' ? report?.downloads?.prepared?.key : action === 'rollback' ? report?.resource_store?.previous : undefined;
  const title = ({activate: '激活候选包', rollback: '回滚上一包', 'import-current': '导入现有资源'})[action];
  if (await confirm(title, `${target ?? `现有版本 ${report?.local_resources?.version}`}。这会改变磁盘资源，下次冷启动选用；不会重启游戏。`)) await run(action, target);
});
el('save-network').addEventListener('click', () => {
  const port = el('proxy-port').value;
  try { command('network', el('proxy-enabled').checked ? port : 'system'); }
  catch { banner('请输入 1–65535 之间的整数端口。'); return; }
  run('network', el('proxy-enabled').checked ? port : 'system');
});
el('proxy-enabled').addEventListener('change', controls);
el('apply-font').addEventListener('click', () => {
  const target = el('font-select')?.value;
  if (target) run('set-font', target);
});
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
else banner('需要从支持 KernelSU WebUI 的 Root 管理器打开本模块页面。当前环境没有 Root 桥接，操作已禁用。');
