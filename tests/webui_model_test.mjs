import test from 'node:test';
import assert from 'node:assert/strict';
import {command, parseResponse, model, bytes, when} from '../module/webroot/view-model.mjs';

test('commands use only fixed actions and reviewed safe operands', () => {
  assert.match(command('activate', 'version-123-abc'), /activate 'version-123-abc' --json$/);
  assert.match(command('rollback', 'old-pack'), /rollback 'old-pack'/);
  assert.match(command('network', '7890'), /network 7890/);
  assert.match(command('cleanup', 'a'.repeat(64)), /cleanup a{64}/);
  for (const [action, operand] of [['download', 'injected'], ['status;id'], ['activate', "x';id"], ['rollback', '../bad'], ['activate', '..'], ['network', '0'], ['network', '65536'], ['network', '0123'], ['cleanup', 'x']]) {
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
});
