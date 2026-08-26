export function createWasmEngine(ctx) {
  const worker = new Worker(new URL('./worker.js', import.meta.url), { type: 'module' });
  let ready = false;
  let failed = false;
  let seq = 0;
  const pending = new Map();

  const failAll = (message) => {
    for (const job of pending.values()) job.reject(new Error(message));
    pending.clear();
  };

  const markFailed = (message) => {
    if (failed) return;
    failed = true;
    ready = false;
    ctx.setStatus(message, true);
    failAll(message);
    ctx.onStateChange();
  };

  const loadTimer = setTimeout(() => {
    if (!ready && !failed) markFailed('Engine took too long to load. Reload the page to retry.');
  }, 30000);

  worker.addEventListener('error', (e) => {
    clearTimeout(loadTimer);
    markFailed(`Worker failed to load: ${e.message || 'see the browser console'}`);
  });

  worker.addEventListener('messageerror', () => {
    clearTimeout(loadTimer);
    markFailed('Worker message could not be delivered');
  });

  worker.addEventListener('message', (e) => {
    const { id, ready: isReady, ok, results, error, verifyValid } = e.data;
    if (isReady !== undefined) {
      clearTimeout(loadTimer);
      if (isReady) {
        ready = true;
        ctx.setStatus('');
        ctx.onStateChange();
      } else {
        markFailed(`Engine failed to load: ${error}`);
      }
      return;
    }
    const job = pending.get(id);
    if (!job) return;
    pending.delete(id);
    if (verifyValid !== undefined) job.resolve(verifyValid);
    else if (ok) job.resolve(results);
    else job.reject(new Error(error));
  });

  const post = (msg) =>
    new Promise((resolve, reject) => {
      if (failed) {
        reject(new Error('engine unavailable'));
        return;
      }
      const id = ++seq;
      pending.set(id, { resolve, reject });
      worker.postMessage({ id, ...msg });
    });

  return {
    isReady: () => ready,
    verifyPassword: (file) =>
      post({ verify: true, bytes: file.bytes, password: file.password || '' }),
    compress: (file, jobs, targetSizeBytes) =>
      post({ bytes: file.bytes, jobs, password: file.password || '', targetSizeBytes }),
  };
}
