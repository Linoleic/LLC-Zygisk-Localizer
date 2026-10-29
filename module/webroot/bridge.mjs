const MAX_STDOUT_BYTES = 512 * 1024;
const MAX_STDERR_BYTES = 64 * 1024;
const LATE_CALLBACK_GRACE_MS = 10 * 60 * 1000;
let callbackSequence = 0;

function makeError(code, message, cause) {
  const error = new Error(message);
  error.name = 'KernelSuBridgeError';
  error.code = code;
  if (cause !== undefined) error.cause = cause;
  return error;
}

function getBridge() {
  if (typeof window === 'undefined' || typeof window.ksu?.exec !== 'function') {
    return null;
  }
  return window.ksu;
}

function nextCallbackName(target) {
  let name;
  do {
    const sequence = callbackSequence++;
    const random = Math.random().toString(36).slice(2, 12) || '0';
    name = `__llcKsuExec_${Date.now().toString(36)}_${sequence.toString(36)}_${random}`;
  } while (Object.prototype.hasOwnProperty.call(target, name));
  return name;
}

function utf8Length(value) {
  return new TextEncoder().encode(value).byteLength;
}

function normalizeResponse(errno, stdout, stderr) {
  if (!Number.isFinite(errno) || !Number.isInteger(errno)) {
    throw makeError('bridge_invalid_response', 'KernelSU bridge returned an invalid errno.');
  }
  if (typeof stdout !== 'string' || typeof stderr !== 'string') {
    throw makeError('bridge_invalid_response', 'KernelSU bridge returned non-string output.');
  }
  if (utf8Length(stdout) > MAX_STDOUT_BYTES) {
    throw makeError('bridge_output_too_large', 'KernelSU stdout exceeded 512 KiB.');
  }
  if (utf8Length(stderr) > MAX_STDERR_BYTES) {
    throw makeError('bridge_output_too_large', 'KernelSU stderr exceeded 64 KiB.');
  }
  return { errno, stdout, stderr };
}

/** Returns whether the KernelSU/SukiSU JavaScript bridge is present. */
export function available() {
  return getBridge() !== null;
}

/**
 * Execute a root shell command through the KernelSU/SukiSU WebUI bridge.
 * A timeout rejects this Promise but cannot cancel a command already accepted by the manager.
 */
export function exec(command, { timeoutMs = 180000 } = {}) {
  if (typeof command !== 'string') {
    return Promise.reject(makeError('bridge_invalid_argument', 'command must be a string.'));
  }
  if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) {
    return Promise.reject(makeError('bridge_invalid_timeout', 'timeoutMs must be a positive finite number.'));
  }

  const bridge = getBridge();
  if (!bridge) {
    return Promise.reject(makeError('bridge_unavailable', 'KernelSU/SukiSU WebUI bridge is unavailable.'));
  }

  const target = window;
  const callbackName = nextCallbackName(target);

  return new Promise((resolve, reject) => {
    let settled = false;
    let timedOut = false;
    let timeoutTimer;
    let lateCleanupTimer;

    const cleanup = () => {
      if (timeoutTimer !== undefined) clearTimeout(timeoutTimer);
      if (lateCleanupTimer !== undefined) clearTimeout(lateCleanupTimer);
      if (Object.prototype.hasOwnProperty.call(target, callbackName)) {
        delete target[callbackName];
      }
    };

    target[callbackName] = (errno, stdout, stderr) => {
      if (timedOut) {
        cleanup();
        return;
      }
      if (settled) return;
      settled = true;
      cleanup();
      try {
        resolve(normalizeResponse(errno, stdout, stderr));
      } catch (error) {
        reject(error);
      }
    };

    timeoutTimer = setTimeout(() => {
      if (settled) return;
      settled = true;
      timedOut = true;
      reject(makeError('bridge_timeout', `Command timed out after ${timeoutMs} ms; the native command may still be running.`));
      // Keep the callback available so a late native evaluateJavascript call still resolves safely.
      lateCleanupTimer = setTimeout(cleanup, LATE_CALLBACK_GRACE_MS);
    }, timeoutMs);

    try {
      bridge.exec(command, JSON.stringify({}), callbackName);
    } catch (cause) {
      if (!settled) {
        settled = true;
        cleanup();
        reject(makeError('bridge_call_failed', 'KernelSU/SukiSU bridge call failed.', cause));
      }
    }
  });
}
