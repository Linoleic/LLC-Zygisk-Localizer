import test from 'node:test';
import assert from 'node:assert/strict';
import {command, parseResponse, model, health, schemaAudit, nextStep, candidateDetails, downloadError, bytes, when} from '../module/webroot/view-model.mjs';

test('schema discovery requires both protocol capabilities and never labels first-baseline candidates as new translations', () => {
  const report = {capabilities: {text_schema_audit: 1}, runtime: {fresh_request: true, report: {kind: 'localization_status',
    runtime: {stage: 'running'}, capabilities: {text_schema_audit: {protocol: 1}}}}};
  assert.equal(model(report).canDiscover, true);
  report.runtime.fresh_request = false; assert.equal(model(report).canDiscover, false);
  report.runtime.fresh_request = true; delete report.capabilities; assert.equal(model(report).canDiscover, false);
  report.text_schema = {state: 'available', first_baseline: true, candidate_containers: [{name: '_new', type: 'TextDataDictionary_New'}],
    container_count: 2, text_class_count: 3, missing_registered: [], delta_counts: {}};
  assert.match(schemaAudit(report).title, /已记录当前游戏的文本格式/);
  assert.match(schemaAudit(report).detail, /供下次检查时比较/);
  assert.match(schemaAudit(report).items[0], /待确认的文本分组/);
  report.text_schema.last_error = 'text_schema_timeout_or_process_changed';
  assert.match(schemaAudit(report).title, /显示上次结果/);
  assert.match(command('discover'), / discover --json$/);
  assert.throws(() => command('discover', 'untrusted'));
});

test('registered empty tables remain observations and cached reports cannot assert current emptiness', () => {
  const report = {text_schema: {state:'available', candidate_containers:[], delta_counts:{}, missing_registered:[],container_count:77,text_class_count:58},
    runtime:{fresh_request:true,report:{kind:'localization_status',runtime:{stage:'running'},coverage:{state:'observed_empty_only',observed_empty_tables:2,
      tables:[{table:'_gachaNotice',sync_state:'observed_empty',snapshot_current:true},{table:'<LobbyBGMCategory>k__BackingField',sync_state:'observed_empty',snapshot_current:true}]}}}};
  assert.equal(health(report).tone,'info'); assert.match(health(report).detail,/游戏加载新内容后会继续汉化/);assert.doesNotMatch(health(report).detail,/验收|快照|覆盖/);
  assert.equal(schemaAudit(report).items.length,2); assert.match(schemaAudit(report).items[0],/抽取公告：目前没有相关文本/);
  assert.match(schemaAudit(report).items[1],/BGM 分类/);
  report.runtime.report.coverage.tables[1].snapshot_current=false;
  assert.equal(schemaAudit(report).items.length,1);
  report.runtime.fresh_request=false;
  assert.equal(schemaAudit(report).items.length,0); assert.doesNotMatch(health(report).detail,/当前为空/);
});

test('health remains independent of candidate activation and prioritizes faults, structure, waiting and coverage', () => {
  const report = {downloads: {prepared: {key: 'new', plan: {version: '123'}}}, resource_store: {state: 'managed'},
    runtime: {fresh_request: true, report: {kind: 'localization_status', runtime: {stage: 'running'},
      compatibility: {status: 'adaptive_approved'}, coverage: {state: 'module_issue', structure_mismatches: 1, missing_local_rows: 38}}}};
  assert.equal(nextStep(report).action, 'activate');
  assert.equal(health(report).tone, 'warning');
  assert.match(health(report).title, /部分文本格式有变化/);
  assert.match(model(report).coverage, /待补译 38/);
  assert.equal(health(report).compatibility, '自动适配');
  const c = report.runtime.report.coverage;
  c.failed_writes = 1; assert.equal(health(report).tone, 'error');
  c.failed_writes = 0; c.structure_mismatches = 0; c.state = 'partial_translation'; c.waiting_or_retrying_tables = 20;
  assert.match(health(report).title, /正在加载汉化/);
  c.waiting_or_retrying_tables = 0; assert.equal(health(report).title, '汉化运行中');assert.equal(health(report).tone, 'info');
  c.missing_local_rows = 0; c.missing_local_subrows = 1; c.retained_original_fields = 1;
  assert.equal(health(report).tone, 'info'); assert.match(model(report).coverage, /附加文本待补 1/);
  c.missing_local_subrows = 0; c.retained_original_fields = 0; c.state = 'observed_applied'; assert.equal(health(report).tone, 'success');
  report.runtime.report.resources = {file_checks: {rejected_files: 1}};
  assert.equal(health(report).tone, 'error');
  report.runtime.report.resources.file_checks = {index_model_issue_files: 1}; assert.equal(health(report).tone, 'info');
  delete report.runtime.report.resources;
  report.runtime.fresh_request = false; report.runtime.state = 'refresh_timeout'; report.runtime.identity_verified = true;
  assert.equal(health(report).tone, 'info'); assert.match(health(report).title, /等待连接游戏/); assert.equal(health(report).canInspect, false);
  report.runtime.state = 'disabled'; assert.equal(health(report).tone, 'error');
  assert.equal(health(null).canInspect, false);
});

test('installed game version is available without a runtime report and never inferred from compatibility labels', () => {
  const report = {game: {state: 'read', version: '1.118.0', version_code: 487}};
  assert.equal(model(report).gameVersion, '1.118.0');
  assert.equal(model(report).gameBuild, 487);
  assert.match(model({runtime: {report: {compatibility: {verified_game_label: 'old-game'}}}}).gameVersion, /未取得/);
  assert.match(model({game: {state: 'unavailable'}}).gameVersion, /未取得/);
});

test('first-use guidance advances from finding a pack to downloading, enabling and verifying a game restart', () => {
  const report = {local_resources: {state: 'unavailable'}, resource_store: {state: 'legacy', can_initialize: true}, updates: {state: 'not_checked'}};
  assert.equal(nextStep(report).label, '开始：查找汉化包');
  report.updates = {state: 'comparison_unavailable', last_success: {remote: {version: '123'}}};
  assert.equal(nextStep(report).action, 'download');
  report.downloads = {last_attempt: {operation: 'download', phase: 'failed', error: 'https_timeout'}};
  assert.match(nextStep(report).title, /下载未完成/);
  assert.equal(nextStep(report).action, 'download');
  report.downloads.prepared = {key: 'new', plan: {version: '123'}};
  assert.equal(nextStep(report).action, 'activate');
  report.resource_store.active = 'new';
  report.resource_store.state = 'managed';
  report.local_resources = {state: 'read', version: '123', pack_key: 'new'};
  report.downloads.last_attempt.phase = 'succeeded';
  assert.equal(nextStep(report).step, 4);
  assert.equal(nextStep(report).action, 'refresh');
  report.runtime = {fresh_request: true, report: {kind: 'localization_status', runtime: {stage: 'running'}, resources: {pack_key: 'new'}}};
  assert.match(nextStep(report).title, /已加载/);
  report.updates.state = 'check_failed';
  assert.equal(nextStep(report).action, 'check');
});

test('next step follows download, activation and restart states without recommending a downgrade', () => {
  assert.equal(nextStep(null).action, null);
  const report = {local_resources: {state: 'read', version: '20261030', pack_key: 'old'},
    resource_store: {state: 'managed', active: 'old'}, updates: {state: 'update_available'}};
  assert.equal(nextStep(report).action, 'download');
  report.downloads = {prepared: {key: 'new', plan: {version: '20261105'}}};
  assert.equal(nextStep(report).action, 'activate');
  report.busy = true; assert.equal(nextStep(report).action, null); delete report.busy;
  report.resource_store.active = 'new'; report.local_resources.pack_key = 'new';
  report.runtime = {fresh_request: true, report: {kind: 'localization_status', runtime: {stage: 'running'}, resources: {pack_key: 'old'}}};
  assert.equal(model(report).canActivate, false);
  assert.equal(model(report).canReload, true);
  assert.equal(nextStep(report).action, 'refresh');
  report.runtime.fresh_request = false; assert.equal(model(report).canReload, false);
  report.downloads.prepared = {key: 'older', plan: {version: '20251030'}};
  report.updates.state = 'no_update';
  assert.equal(nextStep(report).action, 'check');
  report.resource_store = {state: 'legacy'};
  assert.equal(nextStep(report).action, 'check');
  report.downloads.prepared.plan.version = '20261105';
  assert.equal(nextStep(report).action, 'import-current');
  report.resource_store.can_initialize = true;
  assert.equal(nextStep(report).action, 'import-current');
  report.local_resources.state = 'unavailable';
  assert.equal(nextStep(report).action, 'activate');
});

test('module, core and resource identities remain separate and stale cores stay hidden', () => {
  const report = {module: {state: 'read', version: 'v0.4.1', version_code: 491},
    manager_build: {version: 'v0.4.2', version_code: 492, build_id: 'manager-new'},
    local_resources: {version: 2026103002}, runtime: {fresh_request: true, report: {
      kind: 'localization_status', build: {build_id: 'core-old'}, resources: {local_version: 2026103001}
    }}};
  const state = model(report);
  assert.equal(state.moduleVersion, 'v0.4.1（491）');
  assert.equal(state.managerBuild, 'manager-new');
  assert.equal(state.coreBuild, 'core-old');
  assert.equal(state.diskVersion, 2026103002);
  assert.equal(state.runningVersion, '2026103001');
  report.runtime.fresh_request = false;
  assert.equal(model(report).coreBuild, '未取得当前进程构建标识');
  report.runtime.identity_verified = true; report.runtime.state = 'refresh_timeout';
  assert.equal(model(report).coreBuild, 'core-old（同进程缓存）');
  assert.match(model({}).managerBuild, /未提供/);
});

test('commands use only fixed actions and reviewed safe operands', () => {
  assert.match(command('activate', 'version-123-abc'), /activate 'version-123-abc' --json$/);
  assert.match(command('rollback', 'old-pack'), /rollback 'old-pack'/);
  assert.match(command('network', '7890'), /network 7890/);
  assert.match(command('network', 'system', 'ghproxy'), /network system ghproxy --json$/);
  assert.match(command('network', '7890', 'ghfast'), /network 7890 ghfast --json$/);
  assert.match(command('network', 'system', 'custom', 'https://cdn.example.com/'), /network system custom 'https:\/\/cdn\.example\.com\/' --json$/);
  assert.match(command('cleanup', 'a'.repeat(64)), /cleanup a{64}/);
  assert.match(command('fonts'), /fonts --json$/);
  assert.match(command('download-font'), /download-font --json$/);
  assert.match(command('download-font', 'https://example.com/font.7z'), /download-font 'https:\/\/example\.com\/font\.7z' --json$/);
  assert.match(command('set-font', '霞鹜文楷.bundle'), /set-font '霞鹜文楷\.bundle' --json$/);
  assert.match(command('reload'), /reload --json$/);
  assert.match(command('import-font', 'Custom.bundle'), /import-font 'Custom\.bundle' --json$/);
  assert.match(command('import-font', '/sdcard/Download/MyFont.bundle'), /import-font '\/sdcard\/Download\/MyFont\.bundle' --json$/);
  assert.match(command('import-font', '/data/local/tmp/llc_font_123.tmp', 'RealFont.bundle'), /import-font '\/data\/local\/tmp\/llc_font_123\.tmp' 'RealFont\.bundle' --json$/);
  assert.match(command('upload-chunk', 'token1:1', 'SGVsbG8='), /upload-chunk 'token1:1' 'SGVsbG8=' --json$/);
  assert.match(command('language'), /language --json$/);
  assert.match(command('set-language', 'all'), /set-language all --json$/);
  assert.match(command('set-language', 'selective:kr'), /set-language selective kr --json$/);
  assert.match(command('set-language', 'selective:jp'), /set-language selective jp --json$/);
  assert.match(command('set-language', 'selective:en'), /set-language selective en --json$/);
  assert.match(command('detect-proxy'), /detect-proxy --json$/);
  for (const [action, operand] of [['download', 'injected'], ['status;id'], ['activate', "x';id"], ['rollback', '../bad'], ['activate', '..'], ['network', '0'], ['network', '65536'], ['network', '0123'], ['cleanup', 'x'], ['set-font', 'bad;rm'], ['set-font', 'bad$var'], ['import-font', 'bad;rm'], ['import-font', '../bad'], ['upload-chunk', 'bad'], ['upload-chunk', 'bad:9'], ['download-font', 'http://insecure.com'], ['download-font', 'https://bad;url'], ['set-language', 'selective:cn1'], ['set-language', 'bad'], ['set-language', 'all;rm']]) {
    assert.throws(() => command(action, operand));
  }
  assert.throws(() => command('network', 'system', 'bad_mirror'));
  assert.throws(() => command('network', 'system', 'custom', 'http://insecure'));
  assert.throws(() => command('network', 'system', 'custom', 'https://bad;rm'));
  assert.throws(() => command('network', 'system', 'custom', ''));
});
test('disk and current-process resources are distinct even at the same version', () => {
  const report = {local_resources: {version: '123', mode: 'active', pack_key: 'new'}, runtime: {fresh_request: true,
    report: {kind: 'localization_status', pid: 23, resources: {local_version: '123', pack_key: 'old'}, coverage: {missing_local_rows: 19, unsupported_source_fields: 251}}}};
  assert.equal(model(report).runningVersion, '123');
  assert.match(model(report).switchNote, /不同/);
  assert.match(model(report).coverage, /19/);
  report.runtime.fresh_request = false;
  assert.match(model(report).runningVersion, /未取得/);
  assert.doesNotMatch(model(report).coverage, /19/);
  report.runtime.identity_verified = true; report.runtime.state = 'refresh_timeout';
  assert.match(model(report).runningVersion, /同进程缓存/);
  assert.doesNotMatch(model(report).coverage, /19/);
});
test('first installation can activate a verified candidate without a rollback baseline', () => {
  const report = {downloads: {prepared: {key: 'candidate-key'}}, resource_store: {state: 'legacy', can_initialize: true}};
  assert.equal(model(report).canActivate, true);
  report.resource_store.can_initialize = false;
  assert.equal(model(report).canActivate, false);
  report.resource_store.state = 'managed';
  assert.equal(model(report).canActivate, true);
  report.downloads.prepared = null;
  assert.equal(model(report).canActivate, false);
});
test('response schema and formatting are bounded by meaningful unknown labels', () => {
  assert.throws(() => parseResponse({stdout: '{"schema":8,"kind":"module_manager_status"}'}));
  assert.throws(() => parseResponse({stdout: 'not-json'}));
  assert.equal(bytes(undefined), '—'); assert.equal(bytes(1024), '1.0 KiB');
  assert.equal(when(null), '尚无成功记录');
  assert.equal(model(null).canActivate, false);
  assert.equal(model(null).activeFont, '未取得字体信息');
  assert.equal(model(null).fontType, '默认字体包');
  assert.equal(model(null).langMode, 'all');
  assert.equal(model(null).targetLang, 'kr');
  assert.equal(model(null).langModeDisplay, '全部语言强制替换');
  assert.equal(model(null).targetLangDisplay, '全部游戏语言');
  assert.equal(model({language: {mode: 'selective', target_language: 'jp', target_language_name: '日语 (Japanese)'}}).langMode, 'selective');
  assert.equal(model({language: {mode: 'selective', target_language: 'jp', target_language_name: '日语 (Japanese)'}}).targetLang, 'jp');
  assert.equal(model({language: {mode: 'selective', target_language: 'jp', target_language_name: '日语 (Japanese)'}}).langModeDisplay, '仅替换特定语言');
  assert.equal(model({language: {mode: 'selective', target_language: 'jp', target_language_name: '日语 (Japanese)'}}).targetLangDisplay, '日语 (Japanese)');
  assert.equal(model({fonts: {active_font: '得意黑', is_custom: true, available: [{name: '得意黑', is_active: true}]}}).activeFont, '得意黑');
  assert.equal(model({fonts: {active_font: '得意黑', is_custom: true, available: [{name: '得意黑', is_active: true}]}}).fontType, '自定义字体包');
  assert.equal(parseResponse({stdout: '{"schema":1,"kind":"font_manager_summary","active_font":"默认思源黑体"}'}).kind, 'font_manager_summary');
  assert.equal(parseResponse({stdout: '{"schema":1,"kind":"language_manager_summary","mode":"selective","target_language":"kr"}'}).kind, 'language_manager_summary');
  assert.equal(parseResponse({stdout: '{"schema":1,"kind":"reload_manager_summary","game_running":true,"game_pid":1234,"flag_created":true,"message":"ok"}'}).kind, 'reload_manager_summary');
  assert.equal(parseResponse({stdout: '{"schema":1,"kind":"font_upload_chunk_summary","token":"tok","bytes_written":12}'}).kind, 'font_upload_chunk_summary');
  assert.deepEqual(model(null).importableFonts, []);
  assert.equal(model({fonts: {importable: [{name: 'Test.bundle'}]}}).importableFonts.length, 1);
});
test('candidate provenance and activation come from verified candidate and disk state', () => {
  assert.equal(candidateDetails(null).activation, '尚未下载');
  const report = {downloads: {prepared: {key: 'new', verified_at_unix: 1793300000,
    plan: {source: 'official_github_release_asset', size: 2048, sha256: 'a'.repeat(64)}}},
    resource_store: {active: 'old'}, network: {https_transport: 'android_https'}};
  let details = candidateDetails(report);
  assert.equal(details.source, 'LLC 官方 GitHub 发布归档');
  assert.equal(details.archive, '2.0 KiB');
  assert.equal(details.digest, 'a'.repeat(64));
  assert.equal(details.activation, '校验通过 · 等待启用');
  assert.equal(details.transport, 'Android 系统 HTTPS');
  report.resource_store.active = 'new';
  assert.match(candidateDetails(report).activation, /已启用/);
  report.downloads.last_attempt = {phase: 'failed', error: 'https_dns_failed'};
  assert.equal(candidateDetails(report).digest, 'a'.repeat(64));
  assert.match(downloadError('https_tls_verification_failed'), /证书校验失败/);
  assert.match(downloadError('https_response_too_large'), /超过/);
});


test('missing font blocks ready guidance without blocking pack preparation', () => {
  const report = {fonts: {ready: false, active_font: null}, local_resources: {state: 'read', version: '123', pack_key: 'pack'}};
  assert.equal(model(report).activeFont, '尚未准备中文字体');
  assert.equal(nextStep(report).action, 'download-font');
  assert.equal(nextStep(report).label, '一键下载官方中文字体');
  report.fonts.ready = true;
  report.fonts.active_font = 'Chinese.bundle';
  assert.equal(model(report).activeFont, 'Chinese.bundle');
  assert.notEqual(nextStep(report).action, 'download-font');
  report.fonts.ready = false;
  report.local_resources.state = 'missing';
  assert.equal(nextStep(report).action, 'check');
});

test('download errors distinguish denied GitHub metadata from missing assets and throttling', () => {
  assert.match(downloadError('github_api_access_denied'), /GitHub.*可能/);
  assert.match(downloadError('https_rate_limited'), /请求过多/);
  assert.match(downloadError('https_not_found'), /不存在/);
  assert.match(downloadError('https_server_error'), /暂时不可用/);
  assert.match(downloadError('invalid_unity_assetbundle_format'), /AssetBundle/);
  assert.match(downloadError('7z_extract_failed'), /7z/);
  assert.match(downloadError('font_download_failed'), /下载失败/);
});
