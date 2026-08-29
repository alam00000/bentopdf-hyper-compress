import { HYPER_PRESETS } from '../dist/sdk/node/presets.js';
import { normalizeHyperOptions } from '../dist/sdk/node/options.js';
import { renderOptions, sameOptions } from './options-ui.js';
import { icon, mountIcons } from './icons/phosphor.js';

export function initApp(createEngine) {
  mountIcons();
  const $ = (id) => document.getElementById(id);
  const drop = $('drop'), fileInput = $('file'), runBtn = $('run');
  const statusEl = $('status'), resultsEl = $('results'), resultPanel = $('resultPanel');
  const presetSel = $('preset');
  const brotliInput = $('brotli'), pdfaInput = $('preserveConformance');
  const fileListEl = $('fileList'), addStrip = $('addStrip');

  let files = [];
  let running = false;
  let uid = 0;

  const engine = createEngine({
    setStatus: (msg, isError) => setStatus(msg, isError),
    onStateChange: () => refreshChrome(),
  });

  const fmtBytes = (n) => {
    if (n < 1000) return `${n} B`;
    if (n < 1000 * 1000) return `${(n / 1000).toFixed(1)} KB`;
    return `${(n / 1000 / 1000).toFixed(2)} MB`;
  };

  const withIcon = (tag, cls, name, text) => {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    node.appendChild(icon(name));
    const span = document.createElement('span');
    span.textContent = text;
    node.appendChild(span);
    return node;
  };

  function setStatus(msg, isError = false) {
    statusEl.textContent = msg;
    statusEl.className = isError ? 'err' : '';
  }

  function cell(row, text, className) {
    const td = document.createElement('td');
    td.textContent = text;
    if (className) td.className = className;
    row.appendChild(td);
    return td;
  }

  function scanPdfMeta(bytes) {
    const view = new Uint8Array(bytes);
    let encrypted = false;
    let pages = 0;
    const isWs = (b) =>
      b === 0x20 || b === 0x09 || b === 0x0a || b === 0x0b || b === 0x0c || b === 0x0d;
    for (let i = 0; i < view.length; i++) {
      if (view[i] !== 0x2f) continue;
      if (!encrypted &&
          view[i + 1] === 0x45 && view[i + 2] === 0x6e && view[i + 3] === 0x63 &&
          view[i + 4] === 0x72 && view[i + 5] === 0x79 && view[i + 6] === 0x70 &&
          view[i + 7] === 0x74) {
        encrypted = true;
        continue;
      }
      if (view[i + 1] === 0x54 && view[i + 2] === 0x79 && view[i + 3] === 0x70 &&
          view[i + 4] === 0x65) {
        let j = i + 5;
        while (j < view.length && isWs(view[j])) j++;
        if (view[j] === 0x2f && view[j + 1] === 0x50 && view[j + 2] === 0x61 &&
            view[j + 3] === 0x67 && view[j + 4] === 0x65) {
          const next = view[j + 5];
          const letter = next !== undefined &&
            ((next >= 0x41 && next <= 0x5a) || (next >= 0x61 && next <= 0x7a));
          if (!letter) pages++;
        }
      }
    }
    return { encrypted, pages: pages || null };
  }

  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();

  function crc32(data) {
    let c = 0xffffffff;
    for (let i = 0; i < data.length; i++) c = CRC_TABLE[(c ^ data[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  function makeZip(entries) {
    const enc = new TextEncoder();
    const parts = [];
    const central = [];
    let offset = 0;
    for (const e of entries) {
      const nameB = enc.encode(e.name);
      const crc = crc32(e.data);
      const local = new DataView(new ArrayBuffer(30));
      local.setUint32(0, 0x04034b50, true);
      local.setUint16(4, 20, true);
      local.setUint16(6, 0x0800, true);
      local.setUint32(14, crc, true);
      local.setUint32(18, e.data.length, true);
      local.setUint32(22, e.data.length, true);
      local.setUint16(26, nameB.length, true);
      parts.push(new Uint8Array(local.buffer), nameB, e.data);
      const cd = new DataView(new ArrayBuffer(46));
      cd.setUint32(0, 0x02014b50, true);
      cd.setUint16(4, 20, true);
      cd.setUint16(6, 20, true);
      cd.setUint16(8, 0x0800, true);
      cd.setUint32(16, crc, true);
      cd.setUint32(20, e.data.length, true);
      cd.setUint32(24, e.data.length, true);
      cd.setUint16(28, nameB.length, true);
      cd.setUint32(42, offset, true);
      central.push(new Uint8Array(cd.buffer), nameB);
      offset += 30 + nameB.length + e.data.length;
    }
    let cdLen = 0;
    for (const c of central) cdLen += c.length;
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, entries.length, true);
    end.setUint16(10, entries.length, true);
    end.setUint32(12, cdLen, true);
    end.setUint32(16, offset, true);
    return new Blob([...parts, ...central, new Uint8Array(end.buffer)], { type: 'application/zip' });
  }

  function downloadBlob(blob, name) {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 30000);
  }

  function refreshChrome() {
    const has = files.length > 0;
    fileListEl.classList.toggle('hidden', !has);
    addStrip.classList.toggle('hidden', !has);
    drop.classList.toggle('hidden', has);
    $('clearBtn').disabled = !has || running;
    runBtn.disabled = !engine.isReady() || !has || running;
  }

  function statusNode(f) {
    const div = document.createElement('div');
    div.className = 'tool-file-status';
    let span;
    if (f.status === 'compressing') {
      span = withIcon('span', 'busy', 'hourglass-medium', 'Compressing...');
    } else if (f.status === 'failed') {
      span = withIcon('span', 'err', 'x-circle', f.error || 'failed');
    } else if (f.status === 'done' && f.result) {
      const pct = 100 * (1 - f.result.compressedSize / f.result.originalSize);
      const text = f.result.compressedSize >= f.result.originalSize
        ? 'No gain, original kept'
        : `${fmtBytes(f.result.compressedSize)} (${pct >= 0.05 ? '-' + pct.toFixed(1) + '%' : 'same'})`;
      span = withIcon('span', 'ok', 'check-circle', text);
    } else {
      return null;
    }
    div.appendChild(span);
    if (f.status === 'done' && f.result && f.result.warnings && f.result.warnings.length) {
      div.appendChild(withIcon('span', 'warn', 'warning', f.result.warnings.join(' · ')));
    }
    return div;
  }

  const rowByUid = new Map();

  function buildFileRow(f, i) {
    const li = document.createElement('li');
    li.className = 'tool-file-row';

    const num = document.createElement('span');
    num.className = 'tool-file-num';
    num.textContent = String(i + 1);

    const meta = document.createElement('div');
    meta.className = 'tool-file-meta';
    const name = document.createElement('div');
    name.className = 'tool-file-name';
    name.textContent = f.name.replace(/\.pdf$/i, '');
    name.title = f.name;
    const sub = document.createElement('div');
    sub.className = 'tool-file-sub';
    const bits = [];
    if (f.pages) bits.push(`${f.pages} page${f.pages === 1 ? '' : 's'}`);
    bits.push(fmtBytes(f.bytes.byteLength));
    sub.append(bits.join(' '));
    const dots = document.createElement('span');
    dots.className = 'dot';
    dots.textContent = ' · ';
    if (f.encrypted) {
      sub.append(dots.cloneNode(true));
      const lock = withIcon('span', 'lock-badge' + (f.verified ? ' unlocked' : ''),
        f.verified ? 'lock-open' : 'lock', f.verified ? 'Unlocked' : 'Locked');
      if (f.verified) {
        lock.title = 'Change password';
        lock.addEventListener('click', () => { f.verified = false; updateFileRow(f); });
      }
      sub.append(lock);
    }
    meta.append(name, sub);
    const st = statusNode(f);
    if (st) meta.append(st);
    if (f.encrypted && !running && !f.verified) {
      const pwWrap = document.createElement('div');
      pwWrap.className = 'tool-file-pw';
      const pw = document.createElement('input');
      pw.type = 'password';
      pw.placeholder = 'password';
      pw.autocomplete = 'off';
      pw.value = f.password || '';
      pw.addEventListener('input', () => { f.password = pw.value; });
      const unlock = withIcon('button', 'btn-ghost', 'lock-key', 'Unlock');
      unlock.type = 'button';
      const doUnlock = async () => {
        f.password = pw.value;
        if (!f.password) return;
        unlock.disabled = true;
        unlock.lastChild.textContent = 'Checking...';
        try {
          f.verified = await engine.verifyPassword(f);
          f.pwError = f.verified ? '' : 'Wrong password';
        } catch (err) {
          f.verified = false;
          f.pwError = (err && err.message) || 'Could not verify';
        }
        updateFileRow(f);
      };
      unlock.addEventListener('click', doUnlock);
      pw.addEventListener('keydown', (e) => { if (e.key === 'Enter') doUnlock(); });
      pwWrap.append(pw, unlock);
      if (f.pwError) {
        const err = document.createElement('span');
        err.className = 'pw-err';
        err.textContent = f.pwError;
        pwWrap.append(err);
      }
      meta.append(pwWrap);
    }

    const actions = document.createElement('div');
    actions.className = 'tool-file-actions';
    if (f.status === 'done' && f.result && f.result.data) {
      const save = withIcon('button', 'btn-ghost', 'download-simple', 'Save');
      save.type = 'button';
      save.addEventListener('click', () => {
        downloadBlob(new Blob([f.result.data], { type: 'application/pdf' }),
          f.name.replace(/\.pdf$/i, '') + `-${f.result.preset}.pdf`);
      });
      actions.append(save);
    }
    const rm = document.createElement('button');
    rm.className = 'icon-x';
    rm.type = 'button';
    rm.appendChild(icon('x'));
    rm.title = 'Remove';
    rm.disabled = running;
    rm.addEventListener('click', () => {
      const idx = files.indexOf(f);
      if (idx >= 0) files.splice(idx, 1);
      renderFileRows();
      refreshChrome();
    });
    actions.append(rm);

    li.append(num, meta, actions);
    return li;
  }

  function renderFileRows() {
    rowByUid.clear();
    fileListEl.replaceChildren();
    files.forEach((f, i) => {
      const li = buildFileRow(f, i);
      rowByUid.set(f.uid, li);
      fileListEl.append(li);
    });
  }

  function updateFileRow(f) {
    const old = rowByUid.get(f.uid);
    if (!old) {
      renderFileRows();
      return;
    }
    const li = buildFileRow(f, files.indexOf(f));
    rowByUid.set(f.uid, li);
    old.replaceWith(li);
  }

  async function accept(fileList) {
    const picked = [...fileList].filter((f) => f && (/\.pdf$/i.test(f.name) || f.type === 'application/pdf'));
    if (!picked.length) return;
    for (const f of picked) {
      if (files.some((x) => x.name === f.name && x.bytes.byteLength === f.size)) continue;
      const bytes = await f.arrayBuffer();
      const meta = scanPdfMeta(bytes);
      files.push({
        uid: ++uid,
        name: f.name,
        bytes,
        encrypted: meta.encrypted,
        pages: meta.pages,
        password: '',
        status: 'idle',
      });
    }
    renderFileRows();
    refreshChrome();
  }

  for (const el of [drop, addStrip]) {
    el.addEventListener('click', () => fileInput.click());
    ['dragenter', 'dragover'].forEach((ev) =>
      el.addEventListener(ev, (e) => { e.preventDefault(); el.classList.add('over'); }));
    ['dragleave', 'drop'].forEach((ev) =>
      el.addEventListener(ev, () => el.classList.remove('over')));
    el.addEventListener('drop', (e) => {
      e.preventDefault();
      accept(e.dataTransfer.files);
    });
  }
  drop.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') fileInput.click(); });
  fileInput.addEventListener('change', () => { accept(fileInput.files); fileInput.value = ''; });
  $('addBtn').addEventListener('click', () => fileInput.click());
  $('clearBtn').addEventListener('click', () => {
    files = [];
    resultsEl.replaceChildren();
    resultPanel.classList.add('hidden');
    renderFileRows();
    refreshChrome();
  });

  let options = normalizeHyperOptions(HYPER_PRESETS.medium);

  function matchingPreset(opts) {
    return Object.keys(HYPER_PRESETS).find((name) =>
      sameOptions(normalizeHyperOptions(HYPER_PRESETS[name]), opts));
  }

  const setControls = renderOptions($('options'), options, (next) => {
    options = normalizeHyperOptions(next);
    const match = matchingPreset(options);
    presetSel.value = match ?? '__custom';
  });

  function applyPreset(name) {
    options = normalizeHyperOptions({
      ...HYPER_PRESETS[name],
      preserveConformance: options.preserveConformance,
      brotli: options.brotli,
    });
    setControls(options);
  }

  pdfaInput.addEventListener('change', () => {
    options = { ...options, preserveConformance: pdfaInput.checked };
    $('pdfaSwitch').classList.toggle('off', !pdfaInput.checked);
  });

  brotliInput.addEventListener('change', () => {
    options = { ...options, brotli: brotliInput.checked };
    $('brotliSwitch').classList.toggle('off', !brotliInput.checked);
  });

  presetSel.addEventListener('change', () => {
    const v = presetSel.value;
    if (v === '__custom' || v === '__all') return;
    applyPreset(v);
  });

  $('reset').addEventListener('click', () => {
    const v = presetSel.value;
    applyPreset(v === '__custom' || v === '__all' ? 'medium' : v);
    if (v === '__custom') presetSel.value = 'medium';
  });

  function renderCompareRows(results, file, multi) {
    for (const r of results) {
      const tr = document.createElement('tr');
      const label = multi ? `${file.name} - ${r.preset}` : r.preset;
      const labelTd = cell(tr, label);
      if (r.warnings && r.warnings.length) labelTd.title = r.warnings.join('; ');
      if (r.error) {
        const td = cell(tr, r.error, 'err');
        td.colSpan = 5;
        resultsEl.appendChild(tr);
        continue;
      }
      cell(tr, fmtBytes(r.originalSize));
      cell(tr, fmtBytes(r.compressedSize));
      const pct = 100 * (1 - r.compressedSize / r.originalSize);
      const unchanged = r.compressedSize >= r.originalSize;
      cell(tr, unchanged ? 'no gain' : `${pct.toFixed(1)}%`, unchanged ? 'saved none' : 'saved');
      cell(tr, `${r.ms} ms`);
      const actions = cell(tr, '');
      if (r.data) {
        const btn = withIcon('button', 'btn-ghost', 'download-simple', 'Save');
        btn.addEventListener('click', () => {
          downloadBlob(new Blob([r.data], { type: 'application/pdf' }),
            file.name.replace(/\.pdf$/i, '') + `-${r.preset}.pdf`);
        });
        actions.appendChild(btn);
      }
      resultsEl.appendChild(tr);
    }
    resultPanel.classList.remove('hidden');
  }

  runBtn.addEventListener('click', async () => {
    if (!files.length || running) return;
    const choice = presetSel.value;
    const compareAll = choice === '__all';
    const jobs = compareAll
      ? ['low', 'medium', 'high', 'lossless'].map((p) => ({
          label: p,
          options: normalizeHyperOptions({
            ...HYPER_PRESETS[p],
            brotli: brotliInput.checked,
            preserveConformance: pdfaInput.checked,
          }),
        }))
      : [{ label: choice === '__custom' ? 'custom' : choice, options }];

    const targetMb = parseFloat($('targetSize').value);
    const targetSizeBytes = Number.isFinite(targetMb) && targetMb > 0
      ? Math.floor(targetMb * 1024 * 1024)
      : undefined;

    running = true;
    runBtn.disabled = true;
    resultsEl.replaceChildren();
    resultPanel.classList.add('hidden');
    const multi = files.length > 1;
    const zipEntries = [];
    try {
      for (let i = 0; i < files.length; i++) {
        const f = files[i];
        f.status = 'compressing';
        f.result = null;
        updateFileRow(f);
        setStatus(multi ? `Compressing ${i + 1} of ${files.length}` : 'Compressing...');
        let results;
        try {
          results = await engine.compress(f, jobs, targetSizeBytes);
        } catch (e) {
          f.status = 'failed';
          f.error = e.message || String(e);
          updateFileRow(f);
          continue;
        }
        if (compareAll) {
          f.status = results.some((r) => !r.error) ? 'done' : 'failed';
          if (f.status === 'done') f.result = results.find((r) => !r.error);
          renderCompareRows(results, f, multi);
        } else {
          const r = results[0];
          if (r.error) {
            f.status = 'failed';
            f.error = r.error;
          } else {
            f.status = 'done';
            f.result = r;
          }
        }
        updateFileRow(f);
        if (multi) {
          for (const r of results) {
            if (r.data) {
              zipEntries.push({
                name: f.name.replace(/\.pdf$/i, '') + `-${r.preset}.pdf`,
                data: r.data,
              });
            }
          }
        }
      }
      if (multi && zipEntries.length) {
        setStatus('Zipping...');
        downloadBlob(makeZip(zipEntries), 'hyper-compressed.zip');
        setStatus('Done. Zip downloaded.');
      } else {
        setStatus('Done.');
      }
    } finally {
      running = false;
      renderFileRows();
      refreshChrome();
    }
  });

  refreshChrome();
}
