export const DPI_CHOICES = [
  { value: 0, label: 'None' },
  { value: 72, label: '72 dpi' },
  { value: 96, label: '96 dpi' },
  { value: 144, label: '144 dpi' },
  { value: 150, label: '150 dpi' },
  { value: 200, label: '200 dpi' },
  { value: 300, label: '300 dpi' },
];

export const SECTIONS = [
  {
    title: 'Images',
    fields: [
      { key: 'imageQuality', type: 'range', label: 'Image quality', min: 20, max: 100,
        help: 'Lower means smaller files and more visible JPEG artefacts.' },
      { key: 'maxDpi', type: 'dpi', label: 'Maximum resolution',
        help: 'Images above this are resampled down. "None" leaves resolution alone.' },
      { key: 'forceDownsample', type: 'bool', label: 'Always resample to that resolution',
        help: 'Resamples even when the image is only slightly above the limit.' },
      { key: 'grayscale', type: 'bool', label: 'Convert to grayscale' },
      { key: 'reduceColor', type: 'bool', label: 'Reduce colour complexity' },
      { key: 'clipImages', type: 'bool', label: 'Clip invisible parts of images' },
      { key: 'removeAlternates', type: 'bool', label: 'Remove alternate images' },
      { key: 'flattenIcc', type: 'bool', label: 'Simplify large colour profiles' },
      { key: 'preferJpx', type: 'bool', label: 'Prefer JPEG 2000 for photos' },
      { key: 'lossless', type: 'bool', label: 'Lossless (no image re-encoding)',
        help: 'Turns off quality, resolution and every other lossy image step.' },
    ],
  },
  {
    title: 'Fonts',
    fields: [
      { key: 'subsetFonts', type: 'bool', label: 'Subset embedded fonts' },
      { key: 'removeStandardFonts', type: 'bool', label: 'Unembed standard fonts' },
      { key: 'unembedAliasedFonts', type: 'bool', label: 'Unembed standard-font look-alikes',
        help: 'Arial, Times New Roman and Courier New map to the built-in equivalents.' },
      { key: 'mergeFonts', type: 'bool', label: 'Merge and de-duplicate font programs' },
    ],
  },
  {
    title: 'Content',
    fields: [
      { key: 'removeAnnots', type: 'bool', label: 'Flatten annotations (keep fields and links)' },
      { key: 'flattenForms', type: 'bool', label: 'Flatten form fields',
        help: 'Form fields stop being fillable.' },
      { key: 'flattenLinks', type: 'bool', label: 'Flatten link annotations',
        help: 'Links stop being clickable.' },
    ],
  },
  {
    title: 'Cleanup',
    fields: [
      { key: 'removeThumbnails', type: 'bool', label: 'Remove page thumbnails' },
      { key: 'removeAppData', type: 'bool', label: 'Remove application data' },
      { key: 'removeThreads', type: 'bool', label: 'Remove article threads' },
      { key: 'removeSpiderInfo', type: 'bool', label: 'Remove web-capture information' },
      { key: 'removeStructTree', type: 'bool', label: 'Remove document structure tree',
        help: 'Drops tagging, which screen readers rely on.' },
      { key: 'removeOutputIntents', type: 'bool', label: 'Remove output intents' },
    ],
  },
];

export const ALL_KEYS = SECTIONS.flatMap((s) => s.fields.map((f) => f.key));

export function sameOptions(a, b) {
  return ALL_KEYS.every((k) => a[k] === b[k]);
}

export function renderOptions(root, initial, onChange) {
  const state = { ...initial };
  const controls = new Map();

  for (const section of SECTIONS) {
    const details = document.createElement('details');
    details.className = 'optgroup';
    const summary = document.createElement('summary');
    summary.textContent = section.title;
    details.appendChild(summary);

    const body = document.createElement('div');
    body.className = 'optbody';

    for (const field of section.fields) {
      body.appendChild(buildField(field, state, controls, () => onChange({ ...state })));
    }
    details.appendChild(body);
    root.appendChild(details);
  }

  return (next) => {
    Object.assign(state, next);
    for (const [key, apply] of controls) apply(state[key]);
  };
}

function buildField(field, state, controls, notify) {
  const wrap = document.createElement('div');
  wrap.className = field.type === 'bool' ? 'optrow check' : 'optrow';

  if (field.type === 'bool') {
    const id = `opt-${field.key}`;
    const input = document.createElement('input');
    input.type = 'checkbox';
    input.id = id;
    input.checked = Boolean(state[field.key]);
    input.addEventListener('change', () => {
      state[field.key] = input.checked;
      notify();
    });
    const label = document.createElement('label');
    label.htmlFor = id;
    label.textContent = field.label;
    wrap.append(input, label);
    controls.set(field.key, (v) => { input.checked = Boolean(v); });
  } else if (field.type === 'range') {
    const id = `opt-${field.key}`;
    const label = document.createElement('label');
    label.htmlFor = id;
    label.textContent = field.label;
    const line = document.createElement('div');
    line.className = 'rangeline';
    const input = document.createElement('input');
    input.type = 'range';
    input.id = id;
    input.min = String(field.min);
    input.max = String(field.max);
    input.value = String(state[field.key]);
    const out = document.createElement('span');
    out.className = 'rangeval';
    out.textContent = String(state[field.key]);
    input.addEventListener('input', () => {
      state[field.key] = Number(input.value);
      out.textContent = input.value;
      notify();
    });
    line.append(input, out);
    wrap.append(label, line);
    controls.set(field.key, (v) => { input.value = String(v); out.textContent = String(v); });
  } else if (field.type === 'dpi') {
    const id = `opt-${field.key}`;
    const label = document.createElement('label');
    label.htmlFor = id;
    label.textContent = field.label;
    const select = document.createElement('select');
    select.id = id;
    for (const choice of DPI_CHOICES) {
      const opt = document.createElement('option');
      opt.value = String(choice.value);
      opt.textContent = choice.label;
      select.appendChild(opt);
    }
    select.value = String(state[field.key]);
    select.addEventListener('change', () => {
      state[field.key] = Number(select.value);
      notify();
    });
    wrap.append(label, select);
    controls.set(field.key, (v) => { select.value = String(v); });
  }

  if (field.help) {
    const help = document.createElement('p');
    help.className = 'opthelp';
    help.textContent = field.help;
    wrap.appendChild(help);
  }
  return wrap;
}
