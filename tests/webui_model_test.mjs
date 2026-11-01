import test from 'node:test';
import assert from 'node:assert/strict';
import {command, parseResponse, model, bytes, when} from '../module/webroot/view-model.mjs';

test('commands use only fixed actions and reviewed safe operands', () => {
  assert.match(command('activate', 'version-123-abc'), /activate 'version-123-abc' --json$/);
  assert.match(command('rollback', 'old-pack'), /rollback 'old-pack'/);
  assert.match(command('network', '7890'), /network 7890/);
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
  for (const [action, operand] of [['download', 'injected'], ['status;id'], ['activate', "x';id"], ['rollback', '../bad'], ['activate', '..'], ['network', '0'], ['network', '65536'], ['network', '0123'], ['cleanup', 'x'], ['set-font', 'bad;rm'], ['set-font', 'bad$var'], ['import-font', 'bad;rm'], ['import-font', '../bad'], ['upload-chunk', 'bad'], ['upload-chunk', 'bad:9'], ['set-language', 'selective:cn1'], ['set-language', 'bad'], ['set-language', 'all;rm']]) {
    assert.throws(() => command(action, operand));
  }
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
