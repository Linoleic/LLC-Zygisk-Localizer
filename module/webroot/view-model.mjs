const manager = '/data/adb/modules/llc_zygisk/bin/llc-manager';
const keyPattern = /^[A-Za-z0-9_.-]{1,160}$/;
const simple = new Set(['status', 'progress', 'check', 'download', 'import-current', 'cleanup-plan', 'fonts', 'language', 'reload', 'detect-proxy', 'discover']);
export function command(action, operand, operand2, operand3) {
  if (simple.has(action) && operand === undefined) return `${manager} ${action} --json`;
  if ((action === 'activate' || action === 'rollback') && typeof operand === 'string' && keyPattern.test(operand) && operand !== '.' && operand !== '..') {
    return `${manager} ${action} '${operand}' --json`;
  }
  if (action === 'cleanup' && typeof operand === 'string' && /^[a-f0-9]{64}$/.test(operand)) return `${manager} cleanup ${operand} --json`;
  if (action === 'network' && (operand === 'system' || (/^[1-9][0-9]{0,4}$/.test(operand) && Number(operand) <= 65535))) {
    let cmd = `${manager} network ${operand}`;
    if (operand2) {
      if (!['official', 'ghproxy', 'ghfast', 'custom'].includes(operand2)) throw new Error('操作或参数无效');
      cmd += ` ${operand2}`;
      if (operand2 === 'custom') {
        if (typeof operand3 !== 'string' || !/^https:\/\/[A-Za-z0-9._~:/?#[\]@!$&'()*+,;=-]{3,200}$/.test(operand3) || /[;&|`$\n\r\t]/.test(operand3)) {
          throw new Error('操作或参数无效');
        }
        cmd += ` '${operand3.replace(/'/g, "\\'")}'`;
      }
    }
    return `${cmd} --json`;
  }
  if (action === 'set-font' && typeof operand === 'string' && operand.length > 0 && operand.length <= 160 && !/[;&|`$\n\r\t]/.test(operand) && operand !== '.' && operand !== '..') {
    return `${manager} set-font '${operand.replace(/'/g, "\\'")}' --json`;
  }
  if (action === 'import-font' && typeof operand === 'string' && operand.length > 0 && operand.length <= 260 && !/[;&|`$\n\r\t]/.test(operand) && !operand.includes('..')) {
    let cmd = `${manager} import-font '${operand.replace(/'/g, "\\'")}'`;
    if (typeof operand2 === 'string' && operand2.length > 0 && operand2.length <= 160 && !/[;&|`$\n\r\t]/.test(operand2) && !operand2.includes('..')) {
      cmd += ` '${operand2.replace(/'/g, "\\'")}'`;
    }
    return `${cmd} --json`;
  }
  if (action === 'upload-chunk' && typeof operand === 'string' && /^[a-z0-9_-]{1,32}:[01]$/.test(operand) && typeof operand2 === 'string' && /^[A-Za-z0-9+/=]*$/.test(operand2)) {
    return `${manager} upload-chunk '${operand}' '${operand2}' --json`;
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
    'module_cleanup_plan', 'module_cleanup_result', 'font_manager_summary', 'language_manager_summary', 'reload_manager_summary', 'font_upload_chunk_summary', 'proxy_detection_result'].includes(doc.kind)) throw new Error('管理器返回了无法识别的状态');
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
export function downloadError(reason) {
  const schemaErrors = {
    text_schema_live_core_required: '请先进入游戏主页并刷新运行状态',
    text_schema_core_unsupported: '请更新模块后再检查文本适配',
    text_schema_timeout_or_process_changed: '未能完成检查，请回到游戏后重试',
    text_schema_incomplete: '检查未完成，已保留上次结果',
    text_schema_baseline_too_large: '检查结果过大，已保留上次结果'
  };
  if (schemaErrors[reason]) return schemaErrors[reason];
  if (typeof reason === 'string' && reason.startsWith('invalid translation: '))
    return `汉化包中的翻译文件格式不符合要求（${reason.slice(21)}）`;
  const messages = {
    https_client_unavailable: '下载组件不可用，请重新安装完整模块',
    https_dns_failed: '无法解析服务器地址，请检查网络或代理',
    https_connection_failed: '无法连接服务器，请检查网络或代理',
    https_timeout: '连接或下载超时，可以重试',
    https_http_error: '服务器返回错误，请稍后重试',
    https_redirect_refused: '服务器重定向不符合下载要求',
    https_tls_verification_failed: '证书校验失败，请检查设备时间和网络',
    https_tls_failed: 'TLS 连接失败，请检查网络或代理',
    https_response_too_large: '服务器返回的文件超过允许大小',
    https_invalid_url_or_proxy: '下载地址或代理配置不受支持，请使用 HTTP 代理',
    https_request_failed: '网络请求失败，请检查网络或代理',
    archive_sha256_mismatch: '下载文件校验失败，请重新下载',
    archive_download_incomplete: '归档下载不完整，请重试',
    release_changed_during_download: '上游发布在下载期间发生变化，请重新检查更新',
    upstream_version_check_failed: '未能确认上游版本，请先检查更新和网络',
    insufficient_storage_space: '可用存储空间不足',
    import_current_required_for_rollback_baseline: '请先导入现有资源，建立回滚基线',
  };
  return reason ? messages[reason] ?? `操作失败（${reason}）` : '';
}

export function candidateDetails(report) {
  const candidate = report?.downloads?.prepared;
  return {
    source: candidate?.plan?.source === 'official_github_release_asset' ? 'LLC 官方 GitHub 发布归档' : '—',
    archive: candidate?.plan?.size > 0 ? bytes(candidate.plan.size) : '—',
    verified: when(candidate?.verified_at_unix),
    digest: candidate?.plan?.sha256 ?? '尚无已校验的汉化包',
    activation: !candidate ? '尚未下载' : candidate.key === report?.resource_store?.active ? '已启用 · 重开游戏后使用' : '校验通过 · 等待启用',
    transport: ({system_curl: '系统 curl', android_https: 'Android 系统 HTTPS', unavailable: '不可用，请重新安装完整模块'})[report?.network?.https_transport] ?? '未知',
    checkError: downloadError(report?.updates?.last_attempt?.error) || '—',
  };
}

export const coverageLabels = {
  module_issue: '存在写入或结构异常', module_coverage_limited: '部分内容暂时保留原文',
  partial_translation: '部分文本等待翻译', local_translation_missing: '部分文本等待翻译',
  source_evidence_incomplete: '部分内容等待确认', waiting_or_retrying: '等待游戏数据或重试',
  observed_applied: '汉化已应用', disabled: '核心修改已停用',
  observed_empty_only: '等待游戏加载更多内容',
  resource_unavailable: '没有可用资源', api_unavailable: '核心接口不可用', initializing: '核心正在初始化'
};

export function health(report) {
  const run = report?.runtime;
  const result = (tone, title, detail, canInspect = false, compatibility = '') => ({tone, title, detail, canInspect, compatibility});
  if (!report) return result('info', '正在读取运行状态', '进入游戏主页后，点击刷新查看汉化状态。');
  if (['disabled', 'resource_unavailable', 'api_unavailable'].includes(run?.state))
    return result('error', coverageLabels[run.state], '当前尚未确认汉化正常运行，请查看诊断信息。', true);
  if (run?.fresh_request !== true || run.report?.kind !== 'localization_status')
    return result('info', '等待连接游戏', '进入游戏主页后，点击刷新查看最新状态。');
  const current = run.report, coverage = current.coverage;
  const compatibility = ({approved: '当前版本适配已确认', adaptive_approved: '自动适配', approved_by_config: '使用自定义适配设置'})[current.compatibility?.status] ?? '等待确认游戏版本';
  const make = (tone, title, detail) => result(tone, title, detail, true, compatibility);
  if (current.runtime?.stage !== 'running')
    return make('info', '正在准备汉化', '请稍等片刻，准备完成后刷新查看。');
  const files = current.resources?.file_checks;
  if (files?.rejected_files > 0 || ['selection_failed', 'root_unavailable', 'no_usable_files'].includes(current.resources?.state))
    return make('error', '汉化文件未能加载', '部分文件无法读取，请尝试重新下载汉化包；详情见诊断。');
  if (!coverage?.state) return make('info', '游戏已连接', '正在确认汉化状态，请稍后刷新。');
  const count = key => Number.isSafeInteger(coverage[key]) && coverage[key] > 0 ? coverage[key] : 0;
  const faults = count('failed_writes') + count('source_map_mismatches') + count('invalid_runtime_rows');
  if (faults || (coverage.state === 'module_issue' && !count('structure_mismatches')))
    return make('error', '部分汉化未能应用', '请重新打开游戏后重试；如果仍有问题，可在诊断页查看详情。');
  if (count('structure_mismatches'))
    return make('warning', '部分文本格式有变化', '相关内容暂时保留游戏原文，其他汉化继续生效。详情可在诊断页查看。');
  if (count('waiting_or_retrying_tables') || coverage.state === 'waiting_or_retrying')
    return make('info', '正在加载汉化', '游戏数据仍在加载，进入主页后稍等片刻再刷新。');
  if (files?.index_model_issue_files > 0)
    return make('info', '汉化已加载', '部分内容暂时保留游戏原文，可在诊断页查看详情。');
  if (['missing_local_rows', 'missing_local_subrows', 'retained_original_fields', 'empty_source_rows', 'unsupported_source_fields', 'source_uncertain_rows', 'unidentified_runtime_rows'].some(key => count(key)))
    return make('info', '汉化运行中', '可以继续游戏，部分文本会暂时显示游戏原文。');
  if (coverage.state === 'observed_empty_only')
    return make('info', '汉化已就绪', '当前没有需要处理的文本，游戏加载新内容后会继续汉化。');
  if (coverage.state !== 'observed_applied')
    return make('info', '正在确认汉化状态', coverageLabels[coverage.state] ?? '请稍后刷新，或查看诊断信息。');
  return make('success', '汉化运行正常', '当前汉化已加载，可以返回游戏。');
}

export function model(report) {
  const disk = report?.local_resources ?? {}, run = report?.runtime ?? {};
  const fresh = run.fresh_request === true && run.report?.kind === 'localization_status';
  const cached = !fresh && run.identity_verified === true && run.state === 'refresh_timeout' && run.report?.kind === 'localization_status';
  const resources = fresh || cached ? run.report.resources ?? {} : {};
  const coverage = fresh ? run.report.coverage : null;
  const switched = (fresh || cached) && resources.pack_key !== disk.pack_key;
  const runtimeLabels = {disabled: '核心修改已停用', initializing: '核心正在初始化', resource_unavailable: '核心没有可用资源',
    api_unavailable: '核心接口不可用', refresh_timeout: '报告刷新超时', unavailable: '尚未连接游戏', game_not_running_or_report_unavailable: '尚未连接游戏，进入主页后刷新'};
  const fonts = report?.fonts ?? {};
  const lang = report?.language ?? {};
  const langMode = lang.mode === 'selective' ? 'selective' : 'all';
  const targetLang = lang.target_language ?? 'kr';
  const detectedLang = lang.detected_game_language_name ?? (lang.detected_game_language === 1 ? '英语 (EN)' : lang.detected_game_language === 0 ? '韩语 (KR)' : lang.detected_game_language === 2 ? '日语 (JP)' : '未知');
  return {
    health: health(report),
    canDiscover: fresh && run.report.runtime?.stage === 'running' && report?.capabilities?.text_schema_audit === 1 && run.report.capabilities?.text_schema_audit?.protocol === 1,
    schemaAudit: schemaAudit(report),
    gameVersion: report?.game?.state === 'read' ? report.game.version ?? '未取得' : '未取得已安装游戏版本',
    gameBuild: report?.game?.state === 'read' ? report.game.version_code ?? '未取得' : '未取得',
    diskVersion: disk.version ?? '尚无可用汉化包', diskMode: ({active: '托管 · 当前包', previous: '托管 · 回退包', legacy: '原目录'})[disk.mode] ?? '不可用',
    moduleVersion: report?.module?.state === 'read' ? `${report.module.version}（${report.module.version_code}）` : '未取得已安装模块版本',
    managerBuild: report?.manager_build?.build_id ?? '旧管理器未提供构建标识',
    coreBuild: fresh || cached ? `${run.report.build?.build_id ?? '旧核心未提供构建标识'}${cached ? '（同进程缓存）' : ''}` : '未取得当前进程构建标识',
    runningVersion: resources.local_version ? `${resources.local_version}${cached ? '（同进程缓存）' : ''}` : '未取得当前进程版本',
    runningState: fresh ? `游戏已连接 · PID ${run.report.pid}` : cached ? `刷新超时 · 同进程缓存于 ${when(run.report.generated_at_unix)}` : runtimeLabels[run.state] ?? '未取得当前进程报告',
    switchNote: switched ? '已选汉化包与游戏当前使用的包不同。请完全退出并重开游戏后刷新确认。' : fresh ? '启用或回退其他汉化包后，重开游戏生效。' : '运行信息尚未确认；启用或回退资源后，重开游戏生效。',
    coverage: coverage ? `待补译 ${coverage.missing_local_rows ?? '—'} · 附加文本待补 ${coverage.missing_local_subrows ?? '—'} · 保留原文 ${coverage.retained_original_fields ?? '—'} · 待适配字段 ${coverage.unsupported_source_fields ?? '—'}` : '需要当前进程的有效报告',
    coverageNote: coverage ? `当前状态：${coverageLabels[coverage.state] ?? '请查看详细诊断'}。以下为本次已加载内容的检查结果。` : '进入游戏主页后刷新，即可查看本次检查结果。',
    canActivate: Boolean(report?.downloads?.prepared?.key) && report.downloads.prepared.key !== report?.resource_store?.active && (report?.resource_store?.state === 'managed' || report?.resource_store?.can_initialize === true),
    canReload: fresh && run.report.runtime?.stage === 'running',
    canRollback: Boolean(report?.resource_store?.previous),
    canImport: report?.resource_store?.state === 'legacy' && disk.state === 'read',
    activeFont: fonts.active_font ?? '默认思源黑体',
    fontType: fonts.is_custom ? '自定义 AssetBundle' : '系统内置',
    availableFonts: Array.isArray(fonts.available) ? fonts.available : [],
    importableFonts: Array.isArray(fonts.importable) ? fonts.importable : [],
    langMode,
    targetLang,
    langModeDisplay: langMode === 'selective' ? '仅替换特定语言' : '全部语言强制替换',
    targetLangDisplay: langMode === 'selective' ? (lang.target_language_name ?? targetLang) : '全部游戏语言',
    detectedLangDisplay: detectedLang
  };
}

export function schemaAudit(report) {
  const data = report?.text_schema;
  if (!data || data.state === 'not_checked') return {title: data?.last_error ? '上次检查未完成' : '尚未检查文本适配',
    detail: data?.last_error ? downloadError(data.last_error) : '进入游戏主页后点击检查，记录当前游戏的文本格式。', items: []};
  if (data.state !== 'available') return {title: '暂时无法检查文本适配', detail: downloadError(data.last_error) || '请查看详细诊断。', items: []};
  const counts = data.delta_counts ?? {}, items = [];
  for (const [key, label] of Object.entries({added_containers: '新增文本分组', removed_containers: '移除的文本分组', changed_containers: '文本分组变化',
    added_classes: '新增文本类型', removed_classes: '移除的文本类型', changed_classes: '文本格式变化'})) {
    if (counts[key]) items.push(`${label} ${counts[key]} 项：${(data.delta_examples?.[key] ?? []).join('、')}`);
  }
  for (const row of data.candidate_containers ?? []) items.push(`待确认的文本分组：${row.name}（${row.type}）`);
  const runtime = report?.runtime;
  if (runtime?.fresh_request === true && runtime.report?.kind === 'localization_status' && runtime.report.runtime?.stage === 'running') {
    const names = {'_gachaNotice': '抽取公告', '<LobbyBGMCategory>k__BackingField': 'BGM 分类'};
    for (const table of runtime.report.coverage?.tables ?? []) {
      if (table.sync_state === 'observed_empty' && table.snapshot_current === true)
        items.push(`${names[table.table] ?? table.table}：目前没有相关文本，加载后会自动处理。`);
    }
  }
  if (data.missing_registered?.length) items.push(`有些文本分组需要进一步确认：${data.missing_registered.join('、')}。`);
  return {title: data.last_error ? '本次检查未完成，显示上次结果' : data.outdated_game || data.requires_refresh ? '游戏或模块已更新，建议重新检查' : data.first_baseline ? '已记录当前游戏的文本格式' : '文本适配检查完成',
    detail: `${when(data.checked_at_unix)} · 游戏 ${data.game?.version ?? '未知'} · ${data.container_count} 组文本、${data.text_class_count} 种格式。${data.first_baseline ? '已保存，供下次检查时比较。' : '变化详情见下方列表。'}${data.last_error ? ` ${downloadError(data.last_error)}` : ''}`, items};
}

export function nextStep(report) {
  const state = model(report), candidate = report?.downloads?.prepared;
  const result = (title, detail, action, label, step) => ({title, detail, action, label, step});
  if (!report) return result('正在确认汉化状态', '稍候，这里会告诉你下一步需要做什么。', null, '正在读取…', 0);
  if (report.busy) return result('正在处理，请稍候', '下方已展开操作详情，可查看下载或校验进度。完成后这里会显示下一步。', null, '处理中…', 0);
  const diskVersion = String(report.local_resources?.version ?? ''), candidateVersion = String(candidate?.plan?.version ?? '');
  const older = /^\d+$/.test(diskVersion) && /^\d+$/.test(candidateVersion) && BigInt(candidateVersion) < BigInt(diskVersion);
  if (candidate && !older && report.resource_store?.state === 'legacy' && state.canImport) {
    const failed = report.downloads?.last_attempt?.operation === 'import-current' && report.downloads.last_attempt.phase === 'failed';
    return result(failed ? '上次未能保存现有汉化' : '先保留现有汉化，再启用新版',
      failed ? '旧汉化文件和已下载的包仍然保留。可点击下方按钮重试，具体错误可在“汉化包”页查看。' : '检测到你已有汉化文件。先保存为可恢复的旧包；完成后，这里会引导你启用已下载的新版。',
      'import-current', failed ? '重试保存现有汉化' : '保存现有汉化以便恢复', 3);
  }
  if (state.canActivate && !older)
    return result(`汉化包 ${candidateVersion || '版本未知'} 已下载完成`, '文件校验已通过。点击下方按钮确认启用，然后完全退出并重开游戏。', 'activate', '启用已下载的汉化包', 3);
  const run = report.runtime;
  const fresh = run?.fresh_request === true && run.report?.kind === 'localization_status';
  const resources = fresh ? run.report.resources : null;
  if (resources?.pack_key && report.local_resources?.pack_key && resources.pack_key !== report.local_resources.pack_key)
    return result('汉化包已切换，请重开游戏', '当前游戏进程仍使用原来的包。完全退出并重新打开游戏，进入主页后点击下方按钮确认。无需重启设备。', 'refresh', '我已重开游戏，刷新确认', 4);
  const update = report.updates?.state;
  const downloadable = ['update_available', 'no_update', 'local_ahead', 'comparison_unavailable'].includes(update) && report.updates?.last_success?.remote?.version != null;
  if (!candidate && report.downloads?.last_attempt?.operation === 'download' && report.downloads.last_attempt.phase === 'failed')
    return result('汉化包下载未完成', `${downloadError(report.downloads.last_attempt.error) || '上次下载未完成'}。可以重试；需要时点击下方按钮检查网络设置。`, downloadable ? 'download' : 'check', downloadable ? '重试下载汉化包' : '重新查找汉化包', downloadable ? 2 : 1);
  if (update === 'update_available' || (report.local_resources?.state !== 'read' && downloadable))
    return result(report.local_resources?.state === 'read' ? '有新版汉化包可以下载' : '找到汉化包，先下载到设备', '下载后会自动校验文件；完成后再由你确认启用，不会直接替换当前汉化。', 'download', '下载汉化包并校验', 2);
  if (older) return result('已下载的包早于当前汉化', '先检查是否有新版。需要恢复旧包时，可展开“更新详情与手动操作”。', 'check', '重新查找汉化包', 1);
  if (['check_failed', 'check_interrupted'].includes(update))
    return result('暂时未能查到新版汉化包', `${downloadError(report.updates?.last_attempt?.error) || '上次检查未完成'}。可重试；仍失败时，查看下方“网络与下载加速”。现有汉化不受此次检查影响。`, 'check', '重试查找汉化包', 1);
  if (report.local_resources?.state !== 'read')
    return result('还没有可用的汉化包', '先查找汉化包，再按这里的提示下载、启用。字体和语言设置可以先保留默认值。', 'check', '开始：查找汉化包', 1);
  if (fresh && run.report.runtime?.stage === 'running' && resources?.pack_key && resources.pack_key === report.local_resources?.pack_key)
    return result('当前游戏已加载所选汉化包', '可以返回游戏。需要更新汉化时，点击下方按钮检查新版。', 'check', '检查汉化包更新', 0);
  return result('汉化文件已就绪，下一步打开游戏', '如果游戏已在运行，请完全退出再打开。进入主页后点击“刷新状态”，确认游戏使用的汉化包；也可先检查是否有新版。', 'refresh', '我已进入主页，刷新确认', 4);
}
