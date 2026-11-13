use std::collections::{BTreeMap, HashMap, HashSet};

use serde::Serialize;
use serde_json::{Map, Value};

#[derive(Debug, Clone, Serialize)]
pub struct FontState {
    pub count: u64,
    pub embedded: bool,
    pub subset: bool,
    pub bytes: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct FontReport {
    pub name: String,
    #[serde(rename = "type")]
    pub kind: String,
    pub action: &'static str,
    pub before: FontState,
    pub after: Option<FontState>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImageState {
    pub width: u64,
    pub height: u64,
    pub color_space: String,
    pub codec: String,
    pub bytes: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct ImageReport {
    pub page: u64,
    pub name: String,
    pub dpi: Option<u64>,
    pub before: ImageState,
    pub after: Option<ImageState>,
}

#[derive(Debug, Clone, Serialize)]
pub struct CleanupReport {
    pub item: String,
    pub before: u64,
    pub after: u64,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Report {
    pub original_size: u64,
    pub compressed_size: u64,
    pub fonts: Vec<FontReport>,
    pub images: Vec<ImageReport>,
    pub cleanup: Vec<CleanupReport>,
}

struct Doc<'a> {
    json: &'a Value,
    objects: Option<&'a Map<String, Value>>,
}

fn is_ref(s: &str) -> bool {
    s.ends_with(" R")
}

impl<'a> Doc<'a> {
    fn new(json: &'a Value) -> Self {
        let objects = json.get("qpdf").and_then(|q| q.get(1)).and_then(Value::as_object);
        Doc { json, objects }
    }

    fn body(o: &'a Value) -> Option<&'a Value> {
        match o.get("stream") {
            Some(s) => s.get("dict"),
            None => o.get("value"),
        }
    }

    fn resolve(&self, v: Option<&'a Value>) -> Option<&'a Value> {
        match v {
            Some(Value::String(s)) if is_ref(s) => self.objects?.get(&format!("obj:{s}")).and_then(Doc::body),
            other => other,
        }
    }

    fn dict(&self, v: Option<&'a Value>) -> Option<&'a Map<String, Value>> {
        self.resolve(v).and_then(Value::as_object)
    }

    fn num(&self, v: Option<&'a Value>) -> u64 {
        self.resolve(v).and_then(Value::as_f64).map(|n| n as u64).unwrap_or(0)
    }

    fn trailer(&self) -> Option<&'a Map<String, Value>> {
        self.dict(self.objects?.get("trailer").and_then(|t| t.get("value")))
    }

    fn root(&self) -> Option<&'a Map<String, Value>> {
        self.dict(self.trailer()?.get("/Root"))
    }

    fn all_dicts(&self) -> Vec<&'a Map<String, Value>> {
        let Some(objs) = self.objects else { return vec![] };
        let mut keys: Vec<&String> = objs.keys().filter(|k| k.as_str() != "trailer").collect();
        keys.sort_by_key(|k| obj_number(k));
        keys.into_iter().filter_map(|k| Doc::body(&objs[k]).and_then(Value::as_object)).collect()
    }

    fn page_dicts(&self) -> Vec<&'a Map<String, Value>> {
        self.json
            .get("pages")
            .and_then(Value::as_array)
            .map(|pages| pages.iter().filter_map(|p| self.dict(p.get("object"))).collect())
            .unwrap_or_default()
    }

    fn name_tree_count(&self, node: Option<&'a Value>, seen: &mut HashSet<*const Map<String, Value>>) -> u64 {
        let Some(d) = self.dict(node) else { return 0 };
        if !seen.insert(d as *const _) {
            return 0;
        }
        let mut n = self.resolve(d.get("/Names")).and_then(Value::as_array).map(|a| (a.len() / 2) as u64).unwrap_or(0);
        if let Some(kids) = self.resolve(d.get("/Kids")).and_then(Value::as_array) {
            for k in kids {
                n += self.name_tree_count(Some(k), seen);
            }
        }
        n
    }

    fn outline_count(&self) -> u64 {
        let Some(root) = self.root() else { return 0 };
        let mut stack = vec![self.dict(root.get("/Outlines")).and_then(|o| o.get("/First"))];
        let mut seen: HashSet<*const Map<String, Value>> = HashSet::new();
        while let Some(v) = stack.pop() {
            let Some(item) = self.dict(v) else { continue };
            if !seen.insert(item as *const _) {
                continue;
            }
            stack.push(item.get("/Next"));
            stack.push(item.get("/First"));
        }
        seen.len() as u64
    }

    fn annots(&self) -> Vec<&'a Map<String, Value>> {
        let mut out = Vec::new();
        for page in self.page_dicts() {
            if let Some(arr) = self.resolve(page.get("/Annots")).and_then(Value::as_array) {
                out.extend(arr.iter().filter_map(|a| self.dict(Some(a))));
            }
        }
        out
    }
}

fn obj_number(key: &str) -> u64 {
    key.trim_start_matches("obj:").split(' ').next().and_then(|n| n.parse().ok()).unwrap_or(u64::MAX)
}

fn name_of(v: Option<&Value>) -> String {
    v.and_then(Value::as_str).map(|s| s.trim_start_matches('/').to_string()).unwrap_or_default()
}

struct FontFacts {
    name: String,
    kind: String,
    embedded: bool,
    subset: bool,
    bytes: u64,
    count: u64,
}

fn font_facts(doc: &Doc) -> Vec<(String, FontFacts)> {
    let mut order: Vec<String> = Vec::new();
    let mut out: HashMap<String, FontFacts> = HashMap::new();
    for d in doc.all_dicts() {
        if d.get("/Type").and_then(Value::as_str) != Some("/Font") {
            continue;
        }
        let Some(subtype) = d.get("/Subtype").and_then(Value::as_str) else { continue };
        if subtype == "/CIDFontType0" || subtype == "/CIDFontType2" {
            continue;
        }
        let raw = d.get("/BaseFont").and_then(Value::as_str).map(|s| s.trim_start_matches('/').to_string()).unwrap_or_else(|| "(unnamed)".into());
        let subset = raw.len() > 7 && raw.as_bytes()[6] == b'+' && raw.as_bytes()[..6].iter().all(u8::is_ascii_uppercase);
        let name = if subset { raw[7..].to_string() } else { raw };
        let mut kind = subtype.trim_start_matches('/').to_string();
        let mut descriptor = doc.dict(d.get("/FontDescriptor"));
        if kind == "Type0" {
            let kid = doc.resolve(d.get("/DescendantFonts")).and_then(Value::as_array).and_then(|k| doc.dict(k.first()));
            descriptor = kid.and_then(|k| doc.dict(k.get("/FontDescriptor")));
            if let Some(s) = kid.and_then(|k| k.get("/Subtype")).and_then(Value::as_str) {
                kind = format!("Type0/{}", s.trim_start_matches('/'));
            }
        }
        let file = descriptor.and_then(|f| f.get("/FontFile").or(f.get("/FontFile2")).or(f.get("/FontFile3")));
        let embedded = kind == "Type3" || file.is_some();
        let bytes = if file.is_some() { doc.num(doc.dict(file).and_then(|f| f.get("/Length"))) } else { 0 };
        let key = format!("{name}\u{0}{kind}");
        match out.get_mut(&key) {
            Some(prev) => {
                prev.count += 1;
                prev.bytes += bytes;
                prev.embedded |= embedded;
                prev.subset &= subset;
            }
            None => {
                order.push(key.clone());
                out.insert(key, FontFacts { name, kind, embedded, subset, bytes, count: 1 });
            }
        }
    }
    order.into_iter().filter_map(|k| out.remove(&k).map(|f| (k, f))).collect()
}

fn image_state(doc: &Doc, r: Option<&Value>) -> Option<ImageState> {
    let d = doc.dict(r.filter(|v| v.is_string()))?;
    let filters: Vec<String> = match doc.resolve(d.get("/Filter")) {
        Some(Value::Array(a)) => a.iter().map(|f| name_of(doc.resolve(Some(f)))).filter(|s| !s.is_empty()).collect(),
        Some(v) => vec![name_of(Some(v))].into_iter().filter(|s| !s.is_empty()).collect(),
        None => vec![],
    };
    let cs = doc.resolve(d.get("/ColorSpace"));
    let head = match cs {
        Some(Value::Array(a)) => a.first(),
        other => other,
    };
    let color_space = if d.get("/ImageMask") == Some(&Value::Bool(true)) {
        "ImageMask".to_string()
    } else {
        Some(name_of(head)).filter(|s| !s.is_empty()).unwrap_or_else(|| "none".into())
    };
    Some(ImageState {
        width: doc.num(d.get("/Width")),
        height: doc.num(d.get("/Height")),
        color_space,
        codec: if filters.is_empty() { "none".into() } else { filters.join("+") },
        bytes: doc.num(d.get("/Length")),
    })
}

const SCRIPT_ACTIONS: [&str; 7] = ["/JavaScript", "/Launch", "/ImportData", "/SubmitForm", "/Rendition", "/Movie", "/Sound"];
const PRINT_KEYS: [&str; 7] = ["/PrintScaling", "/Duplex", "/PickTrayByPDFSize", "/PrintPageRange", "/NumCopies", "/PrintArea", "/PrintClip"];

fn cleanup_counts(doc: &Doc) -> Vec<(&'static str, u64)> {
    let empty = Map::new();
    let root = doc.root().unwrap_or(&empty);
    let names = doc.dict(root.get("/Names"));
    let annots = doc.annots();
    let subtype = |a: &Map<String, Value>| a.get("/Subtype").and_then(Value::as_str).unwrap_or("").to_string();
    let scripted = |d: &Map<String, Value>| -> u64 {
        let s = doc.dict(d.get("/A")).and_then(|a| a.get("/S")).and_then(Value::as_str);
        u64::from(d.contains_key("/AA")) + u64::from(s.is_some_and(|s| SCRIPT_ACTIONS.contains(&s)))
    };
    let ntc = |v: Option<&Value>| doc.name_tree_count(v, &mut HashSet::new());
    let vp = doc.dict(root.get("/ViewerPreferences"));
    let pages = doc.page_dicts();
    let info = doc.dict(doc.trailer().and_then(|t| t.get("/Info")));
    let has = |k: &str| u64::from(root.contains_key(k));
    let name_has = |k: &str| u64::from(names.is_some_and(|n| n.contains_key(k)));
    vec![
        ("documentInfo", info.map(|i| i.len() as u64).unwrap_or(0)),
        ("xmpMetadata", has("/Metadata")),
        ("bookmarks", doc.outline_count()),
        ("attachments", ntc(names.and_then(|n| n.get("/EmbeddedFiles"))) + annots.iter().filter(|a| subtype(a) == "/FileAttachment").count() as u64),
        ("actions", has("/OpenAction") + has("/AA") + ntc(names.and_then(|n| n.get("/JavaScript")))
            + pages.iter().filter(|p| p.contains_key("/AA")).count() as u64
            + annots.iter().map(|a| scripted(a)).sum::<u64>()),
        ("namedDestinations", doc.dict(root.get("/Dests")).map(|d| d.len() as u64).unwrap_or(0) + ntc(names.and_then(|n| n.get("/Dests")))),
        ("printSettings", vp.map(|v| PRINT_KEYS.iter().filter(|k| v.contains_key(**k)).count() as u64).unwrap_or(0)),
        ("annotations", annots.iter().filter(|a| { let s = subtype(a); s != "/Widget" && s != "/Link" }).count() as u64),
        ("formFields", annots.iter().filter(|a| subtype(a) == "/Widget").count() as u64),
        ("links", annots.iter().filter(|a| subtype(a) == "/Link").count() as u64),
        ("thumbnails", pages.iter().filter(|p| p.contains_key("/Thumb")).count() as u64),
        ("structureTree", has("/StructTreeRoot")),
        ("articleThreads", has("/Threads")),
        ("outputIntents", has("/OutputIntents")),
        ("searchIndex", name_has("/IDS") + name_has("/URLS")),
    ]
}

pub fn image_notes(stderr: &str) -> HashMap<String, u64> {
    let mut dpi = HashMap::new();
    for line in stderr.lines() {
        let Some(rest) = line.split("[hyper-report] image obj=").nth(1) else { continue };
        let mut obj = None;
        let mut value = None;
        for (i, part) in rest.split_whitespace().enumerate() {
            if i == 0 {
                obj = part.parse::<u64>().ok();
            } else if let Some(v) = part.strip_prefix("dpi=") {
                value = v.parse::<u64>().ok();
            }
        }
        if let (Some(o), Some(v)) = (obj, value) {
            dpi.entry(format!("{o} 0 R")).or_insert(v);
        }
    }
    dpi
}

pub fn build(before: &Value, after: &Value, original_size: u64, compressed_size: u64, dpi: &HashMap<String, u64>) -> Report {
    let b = Doc::new(before);
    let a = Doc::new(after);
    let after_fonts: BTreeMap<String, FontFacts> = font_facts(&a).into_iter().collect();
    let fonts = font_facts(&b)
        .into_iter()
        .map(|(key, f)| {
            let g = after_fonts.get(&key);
            let action = match g {
                None => "removed",
                Some(g) if f.embedded && !g.embedded => "unembedded",
                Some(g) if g.subset && !f.subset => "subset",
                Some(g) if g.count < f.count => "merged",
                Some(g) if g.bytes < f.bytes => "recompressed",
                Some(_) => "kept",
            };
            FontReport {
                name: f.name.clone(),
                kind: f.kind.clone(),
                action,
                before: FontState { count: f.count, embedded: f.embedded, subset: f.subset, bytes: f.bytes },
                after: g.map(|g| FontState { count: g.count, embedded: g.embedded, subset: g.subset, bytes: g.bytes }),
            }
        })
        .collect();

    let empty = Vec::new();
    let pages_before = before.get("pages").and_then(Value::as_array).unwrap_or(&empty);
    let pages_after = after.get("pages").and_then(Value::as_array).unwrap_or(&empty);
    let mut images = Vec::new();
    for (i, page) in pages_before.iter().enumerate() {
        for img in page.get("images").and_then(Value::as_array).unwrap_or(&empty) {
            let Some(state) = image_state(&b, img.get("object")) else { continue };
            let name = img.get("name").and_then(Value::as_str).unwrap_or("");
            let matched = pages_after
                .get(i)
                .and_then(|p| p.get("images"))
                .and_then(Value::as_array)
                .and_then(|list| list.iter().find(|x| x.get("name").and_then(Value::as_str) == Some(name)));
            images.push(ImageReport {
                page: i as u64 + 1,
                name: name.trim_start_matches('/').to_string(),
                dpi: img.get("object").and_then(Value::as_str).and_then(|o| dpi.get(o).copied()),
                before: state,
                after: image_state(&a, matched.and_then(|m| m.get("object"))),
            });
        }
    }

    let cb = cleanup_counts(&b);
    let ca: HashMap<&str, u64> = cleanup_counts(&a).into_iter().collect();
    let cleanup = cb
        .into_iter()
        .map(|(item, before)| CleanupReport { item: item.to_string(), before, after: ca.get(item).copied().unwrap_or(0) })
        .filter(|r| r.before > 0 || r.after > 0)
        .collect();

    Report { original_size, compressed_size, fonts, images, cleanup }
}
