const manager = '/data/adb/modules/llc_zygisk/bin/llc-manager';
const keyPattern = /^[A-Za-z0-9_.-]{1,160}$/;
const simple = new Set(['status', 'progress', 'check', 'download', 'import-current', 'cleanup-plan', 'fonts', 'language', 'reload']);
export function command(action, operand) {
  if (simple.has(action) && operand === undefined) return `${manager} ${action} --json`;
  if ((action === 'activate' || action === 'rollback') && typeof operand === 'string' && keyPattern.test(operand) && operand !== '.' && operand !== '..') {
    return `${manager} ${action} '${operand}' --json`;
  }
  if (action === 'cleanup' && typeof operand === 'string' && /^[a-f0-9]{64}$/.test(operand)) return `${manager} cleanup ${operand} --json`;
  if (action === 'network' && (operand === 'system' || /^[1-9][0-9]{0,4}$/.test(operand) && Number(operand) <= 65535)) {
    return `${manager} network ${operand} --json`;
  }
  if (action === 'set-font' && typeof operand === 'string' && operand.length > 0 && operand.length <= 160 && !/[;&|`$\n\r\t]/.test(operand) && operand !== '.' && operand !== '..') {
    return `${manager} set-font '${operand.replace(/'/g, "\\'")}' --json`;
  }
  if (action === 'import-font' && typeof operand === 'string' && operand.length > 0 && operand.length <= 260 && !/[;&|`$\n\r\t]/.test(operand) && !operand.includes('..')) {
    return `${manager} import-font '${operand.replace(/'/g, "\\'")}' --json`;
  }
  if (action === 'set-language' && typeof operand === 'string' && /^(all|selective(:[a-z]{2})?)$/.test(operand)) {
    const parts = operand.split(':');
    return `${manager} set-language ${parts[0]}${parts[1] ? ` ${parts[1]}` : ''} --json`;
  }
  throw new Error('操作或参数无效');
}
export function parseResponse(response) {
  const doc = JSON.parse(response.stdout);
  if (!doc || doc.schema !== 1 || !['module_manager_status', 'module_manager_progress', 'module_manager_error',
    'module_cleanup_plan', 'module_cleanup_result', 'font_manager_summary', 'language_manager_summary', 'reload_manager_summary'].includes(doc.kind)) throw new Error('管理器返回了无法识别的状态');
  return doc;
}
export const updateLabels = {
  no_update: '按版本号暂无更新', update_available: '发现新版汉化资源', local_ahead: '本地版本较新，暂不降级',
  not_checked: '尚未检查', checking: '正在检查', manager_busy: '管理工具正忙', check_failed: '检查失败，保留上次结果',
  check_interrupted: '上次检查中断', stale_result: '上次结果已过期，请重新检查', comparison_unavailable: '本地版本无法比较'
};
export const transferLabels = {
  not_started: '尚未下载', working: '处理中', downloading: '下载中', validating: '校验中', succeeded: '上次操作成功',
  failed: '上次操作失败', operation_interrupted: '上次操作中断，请重试', activation_committed: '已提交激活，请刷新确认'
};
export function bytes(value) {
  if (!Number.isFinite(value) || value < 0) return '—';
  if (value < 1024) return `${value} B`;
  return `${(value / (value < 1048576 ? 1024 : 1048576)).toFixed(1)} ${value < 1048576 ? 'KiB' : 'MiB'}`;
}
export function when(value) {
  return Number.isSafeInteger(value) && value > 0 ? new Date(value * 1000).toLocaleString('zh-CN', {hour12: false}) : '尚无成功记录';
}
export function model(report) {
  const disk = report?.local_resources ?? {}, run = report?.runtime ?? {};
  const fresh = run.fresh_request === true && run.report?.kind === 'localization_status';
  const cached = !fresh && run.identity_verified === true && run.state === 'refresh_timeout' && run.report?.kind === 'localization_status';
  const resources = fresh || cached ? run.report.resources ?? {} : {};
  const coverage = fresh ? run.report.coverage : null;
  const switched = (fresh || cached) && resources.pack_key !== disk.pack_key;
  const runtimeLabels = {disabled: '核心修改已停用', initializing: '核心正在初始化', resource_unavailable: '核心没有可用资源',
    api_unavailable: '核心接口不可用', refresh_timeout: '报告刷新超时', unavailable: '游戏未运行或报告不可用'};
  const fonts = report?.fonts ?? {};
  const lang = report?.language ?? {};
  const langMode = lang.mode === 'selective' ? 'selective' : 'all';
  const targetLang = lang.target_language ?? 'kr';
  return {
    diskVersion: disk.version ?? '无法读取', diskMode: ({active: '托管 · 当前包', previous: '托管 · 回退包', legacy: '原目录'})[disk.mode] ?? '不可用',
    runningVersion: resources.local_version ? `${resources.local_version}${cached ? '（同进程缓存）' : ''}` : '未取得当前进程版本',
    runningState: fresh ? `已取得当前进程报告 · PID ${run.report.pid}` : cached ? `刷新超时 · 同进程缓存于 ${when(run.report.generated_at_unix)}` : runtimeLabels[run.state] ?? '未取得当前进程报告',
    switchNote: switched ? '磁盘选中的包与运行进程不同；下次冷启动才会切换，运行进程继续使用原包。' : '激活或回滚只改变下次冷启动的选择，不会强制重载运行中的游戏。',
    coverage: coverage ? `缺少本地条目 ${coverage.missing_local_rows ?? '—'} · 未支持字段 ${coverage.unsupported_source_fields ?? '—'}` : '需要当前进程的有效报告',
    coverageNote: coverage ? `只统计已观察到的表，不代表全游戏覆盖率。状态：${({module_coverage_limited: '部分源字段尚未适配', module_issue: '存在模块异常，请查看诊断', local_translation_missing: '观察到本地缺译'})[coverage.state] ?? coverage.state ?? '未知'}。` : '过期报告不会作为当前运行证据；版本相同也可能有新文本缺译。',
    canActivate: Boolean(report?.downloads?.prepared?.key) && report?.resource_store?.state === 'managed',
    canRollback: Boolean(report?.resource_store?.previous),
    canImport: report?.resource_store?.state === 'legacy' && disk.state === 'read',
    activeFont: fonts.active_font ?? '默认思源黑体',
    fontType: fonts.is_custom ? '自定义 AssetBundle' : '系统内置',
    availableFonts: Array.isArray(fonts.available) ? fonts.available : [],
    importableFonts: Array.isArray(fonts.importable) ? fonts.importable : [],
    langMode,
    targetLang,
    langModeDisplay: langMode === 'selective' ? '仅替换特定语言' : '全部语言强制替换',
    targetLangDisplay: langMode === 'selective' ? (lang.target_language_name ?? targetLang) : '全部游戏语言'
  };
}
