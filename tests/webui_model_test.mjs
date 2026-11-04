import test from 'node:test';
import assert from 'node:assert/strict';
import {command, parseResponse, model, candidateDetails, downloadError, bytes, when} from '../module/webroot/view-model.mjs';

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
  for (const [action, operand] of [['download', 'injected'], ['status;id'], ['activate', "x';id"], ['rollback', '../bad'], ['activate', '..'], ['network', '0'], ['network', '65536'], ['network', '0123'], ['cleanup', 'x'], ['set-font', 'bad;rm'], ['set-font', 'bad$var'], ['import-font', 'bad;rm'], ['import-font', '../bad'], ['upload-chunk', 'bad'], ['upload-chunk', 'bad:9'], ['set-language', 'selective:cn1'], ['set-language', 'bad'], ['set-language', 'all;rm']]) {
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
  assert.equal(model(null).activeFont, '默认思源黑体');
  assert.equal(model(null).fontType, '系统内置');
  assert.equal(model(null).langMode, 'all');
  assert.equal(model(null).targetLang, 'kr');
  assert.equal(model(null).langModeDisplay, '全部语言强制替换');
  assert.equal(model(null).targetLangDisplay, '全部游戏语言');
  assert.equal(model({language: {mode: 'selective', target_language: 'jp', target_language_name: '日语 (Japanese)'}}).langMode, 'selective');
  assert.equal(model({language: {mode: 'selective', target_language: 'jp', target_language_name: '日语 (Japanese)'}}).targetLang, 'jp');
  assert.equal(model({language: {mode: 'selective', target_language: 'jp', target_language_name: '日语 (Japanese)'}}).langModeDisplay, '仅替换特定语言');
  assert.equal(model({language: {mode: 'selective', target_language: 'jp', target_language_name: '日语 (Japanese)'}}).targetLangDisplay, '日语 (Japanese)');
  assert.equal(model({fonts: {active_font: '得意黑', is_custom: true, available: [{name: '得意黑', is_active: true}]}}).activeFont, '得意黑');
  assert.equal(model({fonts: {active_font: '得意黑', is_custom: true, available: [{name: '得意黑', is_active: true}]}}).fontType, '自定义 AssetBundle');
  assert.equal(parseResponse({stdout: '{"schema":1,"kind":"font_manager_summary","active_font":"默认思源黑体"}'}).kind, 'font_manager_summary');
  assert.equal(parseResponse({stdout: '{"schema":1,"kind":"language_manager_summary","mode":"selective","target_language":"kr"}'}).kind, 'language_manager_summary');
  assert.equal(parseResponse({stdout: '{"schema":1,"kind":"reload_manager_summary","game_running":true,"game_pid":1234,"flag_created":true,"message":"ok"}'}).kind, 'reload_manager_summary');
  assert.equal(parseResponse({stdout: '{"schema":1,"kind":"font_upload_chunk_summary","token":"tok","bytes_written":12}'}).kind, 'font_upload_chunk_summary');
  assert.deepEqual(model(null).importableFonts, []);
  assert.equal(model({fonts: {importable: [{name: 'Test.bundle'}]}}).importableFonts.length, 1);
});
test('candidate provenance and activation come from verified candidate and disk state', () => {
  assert.equal(candidateDetails(null).activation, '尚无候选包');
  const report = {downloads: {prepared: {key: 'new', verified_at_unix: 1793300000,
    plan: {source: 'official_github_release_asset', size: 2048, sha256: 'a'.repeat(64)}}},
    resource_store: {active: 'old'}, network: {https_transport: 'android_https'}};
  let details = candidateDetails(report);
  assert.equal(details.source, 'LLC 官方 GitHub 发布归档');
  assert.equal(details.archive, '2.0 KiB');
  assert.equal(details.digest, 'a'.repeat(64));
  assert.equal(details.activation, '已校验 · 待激活');
  assert.equal(details.transport, 'Android 系统 HTTPS');
  report.resource_store.active = 'new';
  assert.match(candidateDetails(report).activation, /已激活/);
  report.downloads.last_attempt = {phase: 'failed', error: 'https_dns_failed'};
  assert.equal(candidateDetails(report).digest, 'a'.repeat(64));
  assert.match(downloadError('https_tls_verification_failed'), /证书校验失败/);
  assert.match(downloadError('https_response_too_large'), /超过/);
});
