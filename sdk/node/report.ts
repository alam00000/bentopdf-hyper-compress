export interface HyperFontReport {
  name: string;
  type: string;
  before: { count: number; embedded: boolean; subset: boolean; bytes: number };
  after: { count: number; embedded: boolean; subset: boolean; bytes: number } | null;
  action: 'kept' | 'subset' | 'unembedded' | 'merged' | 'recompressed' | 'removed';
}

export interface HyperImageReport {
  page: number;
  name: string;
  dpi: number | null;
  before: HyperImageState;
  after: HyperImageState | null;
}

export interface HyperImageState {
  width: number;
  height: number;
  colorSpace: string;
  codec: string;
  bytes: number;
}

export interface HyperCleanupReport {
  item: string;
  before: number;
  after: number;
}

export interface HyperReport {
  originalSize: number;
  compressedSize: number;
  fonts: HyperFontReport[];
  images: HyperImageReport[];
  cleanup: HyperCleanupReport[];
}

type Json = null | boolean | number | string | Json[] | { [k: string]: Json };
type Dict = { [k: string]: Json };

export interface QpdfJson {
  pages?: Array<{
    object?: string;
    images?: Array<{ name?: string; object?: string }>;
  }>;
  qpdf?: [unknown, Record<string, { value?: Json; stream?: { dict?: Json } }>];
}

const isDict = (v: Json | undefined): v is Dict =>
  typeof v === 'object' && v !== null && !Array.isArray(v);

class Doc {
  private readonly objects: Record<string, { value?: Json; stream?: { dict?: Json } }>;

  constructor(readonly json: QpdfJson) {
    this.objects = json.qpdf?.[1] ?? {};
  }

  resolve(v: Json | undefined): Json | undefined {
    if (typeof v === 'string' && / R$/.test(v)) {
      const o = this.objects[`obj:${v}`];
      return o?.stream ? o.stream.dict : o?.value;
    }
    return v;
  }

  dict(v: Json | undefined): Dict | null {
    const r = this.resolve(v);
    return isDict(r) ? r : null;
  }

  num(v: Json | undefined): number {
    const r = this.resolve(v);
    return typeof r === 'number' ? r : 0;
  }

  get trailer(): Dict | null {
    return this.dict(this.objects.trailer?.value);
  }

  get root(): Dict | null {
    return this.dict(this.trailer?.['/Root']);
  }

  allDicts(): Dict[] {
    const out: Dict[] = [];
    for (const [key, o] of Object.entries(this.objects)) {
      if (key === 'trailer') continue;
      const d = o.stream ? o.stream.dict : o.value;
      if (isDict(d)) out.push(d);
    }
    return out;
  }

  pageDicts(): Dict[] {
    return (this.json.pages ?? [])
      .map((p) => this.dict(p.object ?? null))
      .filter((d): d is Dict => d !== null);
  }

  nameTreeCount(node: Json | undefined, seen = new Set<Dict>()): number {
    const d = this.dict(node);
    if (!d || seen.has(d)) return 0;
    seen.add(d);
    const names = this.resolve(d['/Names']);
    let n = Array.isArray(names) ? Math.floor(names.length / 2) : 0;
    const kids = this.resolve(d['/Kids']);
    if (Array.isArray(kids)) for (const k of kids) n += this.nameTreeCount(k, seen);
    return n;
  }

  outlineCount(): number {
    const outlines = this.dict(this.root?.['/Outlines']);
    const stack: Array<Json | undefined> = [outlines?.['/First']];
    const seen = new Set<Dict>();
    while (stack.length > 0) {
      const item = this.dict(stack.pop());
      if (!item || seen.has(item)) continue;
      seen.add(item);
      stack.push(item['/Next'], item['/First']);
    }
    return seen.size;
  }

  annots(): Dict[] {
    const out: Dict[] = [];
    for (const page of this.pageDicts()) {
      const arr = this.resolve(page['/Annots']);
      if (!Array.isArray(arr)) continue;
      for (const a of arr) {
        const d = this.dict(a);
        if (d) out.push(d);
      }
    }
    return out;
  }
}

const SCRIPT_ACTIONS = new Set([
  '/JavaScript', '/Launch', '/ImportData', '/SubmitForm', '/Rendition', '/Movie', '/Sound',
]);
const PRINT_KEYS = [
  '/PrintScaling', '/Duplex', '/PickTrayByPDFSize', '/PrintPageRange', '/NumCopies',
  '/PrintArea', '/PrintClip',
];

function cleanupCounts(doc: Doc): Record<string, number> {
  const root = doc.root ?? {};
  const names = doc.dict(root['/Names']);
  const annots = doc.annots();
  const subtype = (a: Dict): string => (typeof a['/Subtype'] === 'string' ? a['/Subtype'] : '');
  const scripted = (d: Dict | null): number => {
    if (!d) return 0;
    const action = doc.dict(d['/A']);
    const s = action?.['/S'];
    return (d['/AA'] !== undefined ? 1 : 0) +
      (typeof s === 'string' && SCRIPT_ACTIONS.has(s) ? 1 : 0);
  };
  const vp = doc.dict(root['/ViewerPreferences']);
  const pages = doc.pageDicts();
  const info = doc.dict(doc.trailer?.['/Info']);
  return {
    documentInfo: info ? Object.keys(info).length : 0,
    xmpMetadata: root['/Metadata'] !== undefined ? 1 : 0,
    bookmarks: doc.outlineCount(),
    attachments: doc.nameTreeCount(names?.['/EmbeddedFiles']) +
      annots.filter((a) => subtype(a) === '/FileAttachment').length,
    actions: (root['/OpenAction'] !== undefined ? 1 : 0) + (root['/AA'] !== undefined ? 1 : 0) +
      doc.nameTreeCount(names?.['/JavaScript']) +
      pages.filter((p) => p['/AA'] !== undefined).length +
      annots.reduce((n, a) => n + scripted(a), 0),
    namedDestinations: Object.keys(doc.dict(root['/Dests']) ?? {}).length +
      doc.nameTreeCount(names?.['/Dests']),
    printSettings: vp ? PRINT_KEYS.filter((k) => vp[k] !== undefined).length : 0,
    annotations: annots.filter((a) => subtype(a) !== '/Widget' && subtype(a) !== '/Link').length,
    formFields: annots.filter((a) => subtype(a) === '/Widget').length,
    links: annots.filter((a) => subtype(a) === '/Link').length,
    thumbnails: pages.filter((p) => p['/Thumb'] !== undefined).length,
    structureTree: root['/StructTreeRoot'] !== undefined ? 1 : 0,
    articleThreads: root['/Threads'] !== undefined ? 1 : 0,
    outputIntents: root['/OutputIntents'] !== undefined ? 1 : 0,
    searchIndex: (names?.['/IDS'] !== undefined ? 1 : 0) + (names?.['/URLS'] !== undefined ? 1 : 0),
  };
}

interface FontFacts {
  name: string;
  type: string;
  embedded: boolean;
  subset: boolean;
  bytes: number;
}

function fontFacts(doc: Doc): Map<string, FontFacts & { count: number }> {
  const out = new Map<string, FontFacts & { count: number }>();
  for (const d of doc.allDicts()) {
    if (d['/Type'] !== '/Font' || typeof d['/Subtype'] !== 'string') continue;
    if (d['/Subtype'] === '/CIDFontType0' || d['/Subtype'] === '/CIDFontType2') continue;
    const raw = typeof d['/BaseFont'] === 'string' ? d['/BaseFont'].replace(/^\//, '') : '(unnamed)';
    const subset = /^[A-Z]{6}\+/.test(raw);
    const name = subset ? raw.slice(7) : raw;
    let type = d['/Subtype'].replace(/^\//, '');
    let descriptor = doc.dict(d['/FontDescriptor']);
    if (type === 'Type0') {
      const kids = doc.resolve(d['/DescendantFonts']);
      const kid = Array.isArray(kids) ? doc.dict(kids[0]) : null;
      descriptor = doc.dict(kid?.['/FontDescriptor']);
      if (typeof kid?.['/Subtype'] === 'string') type = `Type0/${kid['/Subtype'].replace(/^\//, '')}`;
    }
    const file = descriptor?.['/FontFile'] ?? descriptor?.['/FontFile2'] ?? descriptor?.['/FontFile3'];
    const embedded = type === 'Type3' || file !== undefined;
    const bytes = file !== undefined ? doc.num(doc.dict(file)?.['/Length']) : 0;
    const key = `${name}\u0000${type}`;
    const prev = out.get(key);
    if (prev) {
      prev.count++;
      prev.bytes += bytes;
      prev.embedded ||= embedded;
      prev.subset &&= subset;
    } else {
      out.set(key, { name, type, embedded, subset, bytes, count: 1 });
    }
  }
  return out;
}

function imageState(doc: Doc, ref: string | undefined): HyperImageState | null {
  if (!ref) return null;
  const d = doc.dict(ref);
  if (!d) return null;
  const filter = doc.resolve(d['/Filter']);
  const filters = Array.isArray(filter) ? filter : filter !== undefined ? [filter] : [];
  const cs = doc.resolve(d['/ColorSpace']);
  const csHead = Array.isArray(cs) ? cs[0] : cs;
  const label = (v: Json | undefined): string =>
    typeof v === 'string' ? v.replace(/^\//, '') : '';
  return {
    width: doc.num(d['/Width']),
    height: doc.num(d['/Height']),
    colorSpace: d['/ImageMask'] === true ? 'ImageMask' : label(csHead) || 'none',
    codec: filters.map((f) => label(doc.resolve(f))).filter(Boolean).join('+') || 'none',
    bytes: doc.num(d['/Length']),
  };
}

export function parseImageNotes(stderr: string): Map<string, number> {
  const dpi = new Map<string, number>();
  for (const m of stderr.matchAll(/\[hyper-report\] image obj=(\d+) page=(\d+) dpi=(\d+)/g)) {
    const key = `${m[1]} 0 R`;
    if (!dpi.has(key)) dpi.set(key, Number(m[3]));
  }
  return dpi;
}

export function buildReport(
  before: QpdfJson,
  after: QpdfJson,
  sizes: { originalSize: number; compressedSize: number },
  dpiByObject: Map<string, number>,
): HyperReport {
  const b = new Doc(before);
  const a = new Doc(after);

  const fontsAfter = fontFacts(a);
  const fonts: HyperFontReport[] = [];
  for (const [key, f] of fontFacts(b)) {
    const g = fontsAfter.get(key) ?? null;
    let action: HyperFontReport['action'] = 'kept';
    if (!g) action = 'removed';
    else if (f.embedded && !g.embedded) action = 'unembedded';
    else if (g.subset && !f.subset) action = 'subset';
    else if (g.count < f.count) action = 'merged';
    else if (g.bytes < f.bytes) action = 'recompressed';
    const { name, type, count, embedded, subset, bytes } = f;
    fonts.push({
      name, type, action,
      before: { count, embedded, subset, bytes },
      after: g ? { count: g.count, embedded: g.embedded, subset: g.subset, bytes: g.bytes } : null,
    });
  }

  const images: HyperImageReport[] = [];
  const pagesAfter = after.pages ?? [];
  (before.pages ?? []).forEach((page, i) => {
    for (const img of page.images ?? []) {
      const state = imageState(b, img.object);
      if (!state) continue;
      const match = pagesAfter[i]?.images?.find((x) => x.name === img.name);
      images.push({
        page: i + 1,
        name: (img.name ?? '').replace(/^\//, ''),
        dpi: img.object ? dpiByObject.get(img.object) ?? null : null,
        before: state,
        after: imageState(a, match?.object),
      });
    }
  });

  const cb = cleanupCounts(b);
  const ca = cleanupCounts(a);
  const cleanup = Object.keys(cb)
    .map((item) => ({ item, before: cb[item] ?? 0, after: ca[item] ?? 0 }))
    .filter((r) => r.before > 0 || r.after > 0);

  return { ...sizes, fonts, images, cleanup };
}
