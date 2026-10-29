import assert from 'node:assert/strict';
import test from 'node:test';
import { available, exec } from '../module/webroot/bridge.mjs';

function installBridge(execImpl) {
  const previousWindow = globalThis.window;
  globalThis.window = { ksu: { exec: execImpl } };
  return () => {
    if (previousWindow === undefined) delete globalThis.window;
    else globalThis.window = previousWindow;
  };
}

function callbackFrom(windowObject, command, options) {
  assert.deepEqual(options, '{}');
  const [name] = Object.keys(windowObject).filter((key) => key.startsWith('__llcKsuExec_'));
  assert.ok(name, 'expected a registered global callback');
  assert.equal(typeof windowObject[name], 'function');
  return { name, callback: windowObject[name] };
}

test('available reports bridge presence', () => {
  const restore = installBridge(() => {});
  try {
    assert.equal(available(), true);
    globalThis.window.ksu.exec = undefined;
    assert.equal(available(), false);
  } finally {
    restore();
  }
});

test('exec sends empty options JSON and resolves the bridge tuple', async () => {
  const restore = installBridge((command, options, callbackName) => {
    assert.equal(command, 'echo ready');
    assert.equal(options, '{}');
    assert.match(callbackName, /^__llcKsuExec_[a-z0-9]+_[a-z0-9]+_[a-z0-9]+$/);
    globalThis.window[callbackName](0, 'ready', '');
  });
  try {
    assert.deepEqual(await exec('echo ready'), { errno: 0, stdout: 'ready', stderr: '' });
    assert.equal(Object.keys(globalThis.window).some((key) => key.startsWith('__llcKsuExec_')), false);
  } finally {
    restore();
  }
});

test('exec cleans callback and rejects synchronous bridge errors', async () => {
  const restore = installBridge(() => { throw new Error('native call failed'); });
  try {
    await assert.rejects(exec('false'), (error) => error.code === 'bridge_call_failed');
    assert.equal(Object.keys(globalThis.window).some((key) => key.startsWith('__llcKsuExec_')), false);
  } finally {
    restore();
  }
});

test('exec rejects malformed response tuples and output over the UTF-8 byte caps', async (t) => {
  const cases = [
    ['fractional errno', 0.5, '', '', 'bridge_invalid_response'],
    ['infinite errno', Infinity, '', '', 'bridge_invalid_response'],
    ['non-string stdout', 0, null, '', 'bridge_invalid_response'],
    ['non-string stderr', 0, '', 1, 'bridge_invalid_response'],
    ['stdout above 512 KiB', 0, 'x'.repeat(512 * 1024 + 1), '', 'bridge_output_too_large'],
    ['stderr above 64 KiB', 0, '', 'y'.repeat(64 * 1024 + 1), 'bridge_output_too_large'],
  ];

  for (const [label, errno, stdout, stderr, code] of cases) {
    await t.test(label, async () => {
      const restore = installBridge((_command, _options, callbackName) => {
        globalThis.window[callbackName](errno, stdout, stderr);
      });
      try {
        await assert.rejects(exec('check'), (error) => error.code === code);
        assert.equal(Object.keys(globalThis.window).some((key) => key.startsWith('__llcKsuExec_')), false);
      } finally {
        restore();
      }
    });
  }
});

test('timeout rejects but retains the callback for a late native result', async () => {
  let pending;
  const restore = installBridge((_command, _options, callbackName) => {
    pending = callbackFrom(globalThis.window, _command, _options);
    assert.equal(pending.name, callbackName);
  });
  try {
    await assert.rejects(exec('sleep 1', { timeoutMs: 5 }), (error) => error.code === 'bridge_timeout');
    assert.equal(typeof globalThis.window[pending.name], 'function');
    pending.callback(0, 'late', '');
    assert.equal(Object.hasOwn(globalThis.window, pending.name), false);
  } finally {
    restore();
  }
});

test('timeout fallback removes a callback after the ten minute grace period', async () => {
  const originalSetTimeout = globalThis.setTimeout;
  const originalClearTimeout = globalThis.clearTimeout;
  const scheduled = [];
  let pending;
  const restore = installBridge((_command, _options, callbackName) => {
    pending = callbackFrom(globalThis.window, _command, _options);
    assert.equal(pending.name, callbackName);
  });
  globalThis.setTimeout = (callback, delay) => {
    const timer = { callback, delay, cleared: false };
    scheduled.push(timer);
    return timer;
  };
  globalThis.clearTimeout = (timer) => { if (timer) timer.cleared = true; };

  try {
    const result = exec('long command', { timeoutMs: 17 });
    assert.equal(scheduled[0].delay, 17);
    scheduled[0].callback();
    await assert.rejects(result, (error) => error.code === 'bridge_timeout');
    assert.equal(scheduled[1].delay, 10 * 60 * 1000);
    assert.equal(typeof globalThis.window[pending.name], 'function');
    scheduled[1].callback();
    assert.equal(Object.hasOwn(globalThis.window, pending.name), false);
  } finally {
    globalThis.setTimeout = originalSetTimeout;
    globalThis.clearTimeout = originalClearTimeout;
    restore();
  }
});

test('exec rejects when the bridge is unavailable', async () => {
  const previousWindow = globalThis.window;
  delete globalThis.window;
  try {
    assert.equal(available(), false);
    await assert.rejects(exec('id'), (error) => error.code === 'bridge_unavailable');
  } finally {
    if (previousWindow !== undefined) globalThis.window = previousWindow;
  }
});
