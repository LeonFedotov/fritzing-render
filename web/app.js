// The web demo: loads the WebAssembly renderer and the parts index, fetches
// the parts a sketch names from GitHub (pinned commits) into the renderer's
// file system, and shows the result. Parts you drop are kept in this
// browser (IndexedDB) and searched before the libraries; the catalogue
// searches every part's index entry and renders previews only for the
// results on screen.

import { unzipSync } from 'https://cdn.jsdelivr.net/npm/fflate@0.8.2/esm/browser.js'

const FRITZING_APP = 'https://raw.githubusercontent.com/fritzing/fritzing-app/5aa56a510183c23084990a6b4481708cad24c15b/sketches/core/'

const EXAMPLES = [
  { title: 'Blink', file: 'examples/blink.fzz', about: 'An Arduino Uno blinking an LED through a 220 Ω resistor: a small hand-written .fzz.' },
  { title: 'AnalogInputPot', file: FRITZING_APP + 'AnalogInputPot.fzz', schematic: true, about: "Fritzing's own example: a potentiometer read on A0, an LED on D13." },
  { title: 'PingPong', file: FRITZING_APP + 'Fritzing%20Creator%20Kit%20DE%2BEN/creator-kit-en/Fritzing/PingPong.fzz', schematic: true, theme: 'modern', view: 'schematic', about: 'From the Fritzing Creator Kit: an 8×8 LED matrix game with net labels and power symbols.' },
  { title: 'Button', file: FRITZING_APP + 'Button.fzz', schematic: true, about: "Fritzing's own example: a pushbutton on D2." },
  { title: 'ODROID-SHOW2 station', file: 'examples/odroid-show2.json', about: 'An ODROID-SHOW2 on a LiPo, with a RedBear BLE Nano as its Bluetooth relay (parts from fritzing-parts-extra).' },
  { title: 'FeatherS2 air station', file: 'examples/feathers2-air.json', about: 'A FeatherS2 with PMSA003I, SGP30 and BME680 breakouts and a BLE Nano bridge.' },
  { title: 'ESP32 air station', file: 'examples/esp32-air-station.json', about: 'An ESP32 DevKit with an MH-Z19, AM2302, PMSA003I and a microSD card.' },
  { title: 'BLE Nano on a breadboard', file: 'examples/ble-nano.json', about: 'The RedBear BLE Nano v1.5 part, drawn from its gerbers, straddling the gap.' },
]

const USER_ROOT = 'user-parts'   // /parts/user-parts: searched first by the renderer (src/wasm.cpp)
const status = document.querySelector('#status')
const say = text => { status.textContent = text }

// --- the renderer -----------------------------------------------------------

let renderer
const ready = (async () => {
  renderer = await qtLoad({
    qt: {
      entryFunction: window.fritzing_render_web_entry,
      containerElements: [document.querySelector('#qt')],
      onExit: () => {},
    },
  })
  buildLookup(await (await fetch('parts-index.json')).json())
  await restoreUserParts()
  showUserParts()
  const libraries = new Set(INDEX.parts.map(p => p.r)).size
  say(`Ready: ${INDEX.parts.length.toLocaleString()} parts from ${libraries} libraries.`)
})().catch(e => {
  say('The renderer failed to load: ' + e)
  throw e
})

// --- library parts --------------------------------------------------------------

let INDEX
const byModule = new Map()
const byPath = new Map()
const byTitle = new Map()

function buildLookup(index) {
  INDEX = index
  for (const p of index.parts) {
    if (!byModule.has(p.m)) byModule.set(p.m, p)
    byPath.set(p.p, p)
    byPath.set(`${p.r}/${p.p}`, p)
    const t = p.t.toLowerCase()
    if (!byTitle.has(t)) byTitle.set(t, p)
  }
}

// partlib::resolve, for the parts the index knows: a path, a module id, a title.
const resolve = ref => byPath.get(ref) ?? byModule.get(ref) ?? byTitle.get(ref.toLowerCase())

const url = (base, path) => base + path.split('/').map(encodeURIComponent).join('/')

function write(path, bytes) {
  renderer.FS.mkdirTree(path.slice(0, path.lastIndexOf('/')))
  renderer.FS.writeFile(path, bytes)
}

async function fetchBytes(href) {
  const r = await fetch(href)
  if (!r.ok) throw new Error(`${r.status} for ${href}`)
  return new Uint8Array(await r.arrayBuffer())
}

// One fetch per file or archive, shared by every render that needs it.
const loads = new Map()
const once = (key, work) => {
  if (!loads.has(key)) loads.set(key, work().catch(e => { loads.delete(key); throw e }))
  return loads.get(key)
}

// Fetches a part into /parts/<library>/..., laid out as vendor/ is: loose
// files from the library's repository, or the archive it ships in, unpacked.
function loadPart(p) {
  if (p.z !== undefined) {
    return once('z' + p.z, async () => {
      const folder = p.p.split('/')[0]
      for (const [name, bytes] of Object.entries(unzipSync(await fetchBytes(INDEX.archives[p.z])))) {
        if (!name.endsWith('/')) write(`/parts/${p.r}/${folder}/${name.split('/').pop()}`, bytes)
      }
    })
  }
  const base = INDEX.roots[p.r].base
  return Promise.all([p.p, p.b, p.s].filter(Boolean).map(path =>
    once(`${p.r}/${path}`, async () => write(`/parts/${p.r}/${path}`, await fetchBytes(url(base, path))))))
}

async function loadPartsFor(bytes) {
  const refs = JSON.parse(renderer.modules(bytes))
  // your parts are already in the file system
  const parts = refs.filter(ref => !userPartFor(ref)).map(resolve).filter(Boolean)
  await Promise.all(parts.map(loadPart))
}

// --- your parts -------------------------------------------------------------------
// Each kept as {key, title, moduleId, files: {path in its folder: bytes}} in
// IndexedDB, and written to /parts/user-parts/<key>/ for the renderer.

const USER = new Map()   // key -> part
const userPartFor = ref => [...USER.values()].find(u => u.moduleId === ref || u.title.toLowerCase() === ref.toLowerCase() || `${u.key}/${u.fzp}` === ref)

const db = (() => {
  let opened
  const open = () => opened ??= new Promise((res, rej) => {
    const r = indexedDB.open('fritzing-render', 1)
    r.onupgradeneeded = () => r.result.createObjectStore('parts', { keyPath: 'key' })
    r.onsuccess = () => res(r.result)
    r.onerror = () => rej(r.error)
  })
  const run = async (mode, fn) => {
    try {
      const d = await open()
      return await new Promise((res, rej) => {
        const t = d.transaction('parts', mode)
        const r = fn(t.objectStore('parts'))
        t.oncomplete = () => res(r?.result)
        t.onerror = () => rej(t.error)
      })
    } catch (e) {
      console.warn('browser storage unavailable; parts last until the page closes', e)
      return undefined
    }
  }
  return {
    all: () => run('readonly', s => s.getAll()),
    put: part => run('readwrite', s => s.put(part)),
    del: key => run('readwrite', s => s.delete(key)),
  }
})()

const decoder = new TextDecoder()

function fzpInfo(bytes) {
  const doc = new DOMParser().parseFromString(decoder.decode(bytes).replace(/[\x00-\x08\x0b\x0c\x0e-\x1f]/g, ''), 'application/xml')
  const root = doc.documentElement
  if (!root || root.nodeName !== 'module') return null
  return {
    moduleId: root.getAttribute('moduleId') ?? '',
    title: doc.querySelector('title')?.textContent?.trim() || '(untitled)',
    images: [...doc.querySelectorAll('views layers[image]')].map(l => l.getAttribute('image')),
  }
}

const keyFor = name => name.replace(/[^\w.-]+/g, '_').slice(0, 80)

// Dropped files to parts: each .fzpz is one; each .fzp is one with the .svg
// files dropped with it that its views name (by file name).
async function partsFrom(files) {
  const parts = []
  const svgs = new Map()
  for (const f of files) if (/\.svg$/i.test(f.name)) svgs.set(f.name, new Uint8Array(await f.arrayBuffer()))
  for (const f of files) {
    const bytes = new Uint8Array(await f.arrayBuffer())
    if (/\.fzpz$/i.test(f.name)) {
      const unzipped = Object.fromEntries(Object.entries(unzipSync(bytes)).filter(([n]) => !n.endsWith('/')).map(([n, b]) => [n.split('/').pop(), b]))
      const fzp = Object.keys(unzipped).find(n => n.endsWith('.fzp'))
      const info = fzp && fzpInfo(unzipped[fzp])
      if (!info) throw new Error(`${f.name} has no part file (.fzp) in it`)
      parts.push({ key: keyFor(f.name.replace(/\.fzpz$/i, '')), ...info, fzp, files: unzipped })
    } else if (/\.fzp$/i.test(f.name)) {
      const info = fzpInfo(bytes)
      if (!info) throw new Error(`${f.name} is not a Fritzing part file`)
      const files = { [f.name]: bytes }
      const missing = []
      for (const image of info.images) {
        const svg = svgs.get(image.split('/').pop())
        if (svg) files[image] = svg   // partlib finds <image> beside the .fzp
        else if (/^(breadboard|schematic)\//.test(image)) missing.push(image.split('/').pop())
      }
      parts.push({ key: keyFor(f.name.replace(/\.fzp$/i, '')), ...info, fzp: f.name, files, missing })
    }
  }
  return parts
}

function installUserPart(part) {
  const dir = `/parts/${USER_ROOT}/${part.key}`
  removeTree(dir)
  for (const [path, bytes] of Object.entries(part.files)) write(`${dir}/${path}`, bytes)
  USER.set(part.key, part)
}

function removeTree(path) {
  const FS = renderer.FS
  if (!FS.analyzePath(path).exists) return
  for (const name of FS.readdir(path)) {
    if (name === '.' || name === '..') continue
    const child = `${path}/${name}`
    if (FS.isDir(FS.stat(child).mode)) removeTree(child)
    else FS.unlink(child)
  }
  FS.rmdir(path)
}

async function restoreUserParts() {
  for (const part of (await db.all()) ?? []) installUserPart(part)
}

async function addUserParts(files) {
  await ready
  const parts = await partsFrom(files)
  for (const part of parts) {
    installUserPart(part)
    await db.put({ key: part.key, title: part.title, moduleId: part.moduleId, fzp: part.fzp, files: part.files, missing: part.missing })
  }
  showUserParts(parts.map(p => p.key))
  return parts
}

const userList = document.querySelector('#user-parts')
function showUserParts(fresh = []) {
  userList.replaceChildren()
  document.querySelector('#user-parts-empty').hidden = USER.size > 0
  for (const u of USER.values()) {
    const c = partCard({ title: u.title, lib: 'your parts', moduleId: u.moduleId, ref: u.moduleId, missing: u.missing }, userList, async () => {
      USER.delete(u.key)
      removeTree(`/parts/${USER_ROOT}/${u.key}`)
      await db.del(u.key)
      showUserParts()
    })
    if (fresh.includes(u.key)) c.scrollIntoView({ block: 'nearest' })
  }
}

// --- rendering ----------------------------------------------------------------

async function renderBytes(bytes, view, theme) {
  await ready
  await loadPartsFor(bytes)
  return JSON.parse(renderer.render(bytes, view, theme))
}

const encoder = new TextEncoder()
const sketchOf = ref => encoder.encode(JSON.stringify({ parts: [{ id: 'part', part: ref }], margin: 6 }))

const FONTS = `<link rel="stylesheet" href="${new URL('fonts.css', location.href)}">`

// Each drawing in its own frame: part drawings reuse element ids, and the
// frame can load Fritzing's fonts for the text in them.
function frame(result) {
  const f = document.createElement('iframe')
  f.className = 'drawing'
  f.title = 'drawing'
  f.srcdoc = `<!doctype html><html><head>${FONTS}<style>html,body{margin:0;background:#fff}svg{display:block;width:100%;height:100vh}</style></head><body>${result.svg}</body></html>`
  f.style.aspectRatio = `${result.width} / ${result.height}`
  return f
}

function download(name, blob) {
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = name
  a.click()
  setTimeout(() => URL.revokeObjectURL(a.href), 1000)
}

async function png(result, ppi = 300) {
  const img = new Image()
  img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(result.svg)
  await img.decode()
  const canvas = document.createElement('canvas')
  canvas.width = Math.round(result.width * ppi)
  canvas.height = Math.round(result.height * ppi)
  const g = canvas.getContext('2d')
  g.fillStyle = '#fff'
  g.fillRect(0, 0, canvas.width, canvas.height)
  g.drawImage(img, 0, 0, canvas.width, canvas.height)
  return new Promise(res => canvas.toBlob(res, 'image/png'))
}

// Renders run one at a time, in the order cards come into view.
const queue = []
let busy = false
async function pump() {
  if (busy) return
  busy = true
  while (queue.length) await queue.shift()()
  busy = false
}
const whenSeen = new IntersectionObserver(entries => {
  for (const e of entries) {
    if (!e.isIntersecting) continue
    whenSeen.unobserve(e.target)
    queue.push(e.target._draw)
    pump()
  }
}, { rootMargin: '200px' })

// A sketch card: a title, view and theme switches, the drawing, warnings, downloads.
function card({ title, about, bytes, name, schematic, view = 'breadboard', theme = 'fritzing' }, host) {
  const el = document.createElement('article')
  el.className = 'card'
  el.innerHTML = `<header><h3></h3><p class="about"></p>
    <div class="controls">
      <div class="seg view"><button data-view="breadboard">Breadboard</button><button data-view="schematic">Schematic</button></div>
      <div class="seg theme"><button data-theme="fritzing">Fritzing</button><button data-theme="modern">Modern</button></div>
      <span class="spacer"></span>
      <button class="link svg">SVG</button><button class="link png">PNG</button>
    </div></header>
    <div class="stage"><p class="pending">Waiting…</p></div><p class="warn"></p>`
  el.querySelector('h3').textContent = title
  el.querySelector('.about').textContent = about ?? ''
  host.append(el)
  const state = { view, theme, result: null }
  const views = el.querySelectorAll('[data-view]')
  const themes = el.querySelectorAll('[data-theme]')
  if (!schematic) el.querySelector('.view').hidden = true

  async function draw() {
    views.forEach(b => b.classList.toggle('on', b.dataset.view === state.view))
    themes.forEach(b => b.classList.toggle('on', b.dataset.theme === state.theme))
    el.querySelector('.theme').hidden = state.view !== 'schematic'
    const stage = el.querySelector('.stage')
    stage.innerHTML = '<p class="pending">Fetching parts and rendering…</p>'
    try {
      const data = await bytes()
      const t0 = performance.now()
      const r = await renderBytes(data, state.view, state.theme)
      if (r.error) throw new Error(r.error)
      state.result = r
      stage.replaceChildren(frame(r))
      el.querySelector('.warn').textContent = r.warnings.join(' · ')
      el.dataset.ms = Math.round(performance.now() - t0)
    } catch (e) {
      stage.innerHTML = ''
      const p = document.createElement('p')
      p.className = 'error'
      p.textContent = String(e.message ?? e)
      stage.append(p)
    }
  }
  views.forEach(b => b.onclick = () => { state.view = b.dataset.view; draw() })
  themes.forEach(b => b.onclick = () => { state.theme = b.dataset.theme; draw() })
  el.querySelector('.svg').onclick = () => state.result && download(`${name}.svg`, new Blob([state.result.svg], { type: 'image/svg+xml' }))
  el.querySelector('.png').onclick = async () => state.result && download(`${name}.png`, await png(state.result))
  return { el, draw }
}

// A part card: its preview (rendered when it comes into view), title,
// library, module id, and a sketch line to copy.
function partCard({ title, lib, moduleId, ref, missing }, host, onRemove) {
  const el = document.createElement('article')
  el.className = 'part'
  el.innerHTML = `<div class="stage small"><p class="pending">…</p></div>
    <div class="meta"><h4></h4><p class="lib"></p><code class="mid" title="module id"></code>
      <p class="warn"></p>
      <div class="actions"><button class="link copy">Copy for a sketch</button><button class="link remove" hidden>Remove</button></div></div>`
  el.querySelector('h4').textContent = title
  el.querySelector('.lib').textContent = lib
  el.querySelector('.mid').textContent = moduleId
  if (missing?.length) el.querySelector('.warn').textContent = `Drop its drawings too: ${missing.join(', ')}`
  el.querySelector('.copy').onclick = async e => {
    await navigator.clipboard?.writeText(JSON.stringify({ id: 'p1', part: ref, x: 0, y: 0 }))
    e.target.textContent = 'Copied'
    setTimeout(() => { e.target.textContent = 'Copy for a sketch' }, 1500)
  }
  if (onRemove) {
    const b = el.querySelector('.remove')
    b.hidden = false
    b.onclick = onRemove
  }
  el._draw = async () => {
    const stage = el.querySelector('.stage')
    try {
      const r = await renderBytes(sketchOf(ref), 'breadboard', 'fritzing')
      if (r.error) throw new Error(r.error)
      stage.replaceChildren(frame(r))
    } catch (e) {
      stage.innerHTML = '<p class="error"></p>'
      stage.firstChild.textContent = String(e.message ?? e)
    }
  }
  host.append(el)
  whenSeen.observe(el)
  return el
}

// --- the catalogue ------------------------------------------------------------
// Every query word must appear in a part's title, module id, family, tags,
// library or file name; title matches rank first (partlib::search). Only the
// results on screen are fetched and drawn.

const LIB_NAMES = {
  'fritzing-parts': 'Fritzing', 'adafruit-parts': 'Adafruit', 'sparkfun-parts': 'SparkFun', 'seeed-parts': 'Seeed Studio',
  'mgesteiro-parts': 'mgesteiro', 'dip-ic-parts': 'DIP ICs', 'elegoo-parts': 'Elegoo', 'mkjanke-parts': 'mkjanke',
  'community-parts': 'community', 'fritzing-parts-extra': 'fritzing-parts-extra', [USER_ROOT]: 'your parts',
}
const PAGE = 24
let haystacks

function search(query) {
  const words = query.toLowerCase().split(/\s+/).filter(Boolean)
  if (!words.length) return []
  haystacks ??= INDEX.parts.map(p => [p, `${p.t} ${p.m} ${p.f ?? ''} ${p.g ?? ''} ${LIB_NAMES[p.r] ?? p.r} ${p.p}`.toLowerCase()])
  const mine = [...USER.values()].map(u => [{ t: u.title, m: u.moduleId, r: USER_ROOT, p: `${u.key}/${u.fzp}`, user: true }, `${u.title} ${u.moduleId} your parts`.toLowerCase()])
  const hits = []
  for (const [p, hay] of [...mine, ...haystacks]) {
    if (!words.every(w => hay.includes(w))) continue
    const title = p.t.toLowerCase()
    const score = words.filter(w => title.includes(w)).length * 10 + (p.user ? 5 : 0) + (p.r === 'fritzing-parts' && p.p.startsWith('core/') ? 2 : 0) - (p.p.startsWith('obsolete/') ? 8 : 0)
    hits.push([score, p])
  }
  return hits.sort((a, b) => b[0] - a[0]).map(([, p]) => p)
}

const results = document.querySelector('#results')
const more = document.querySelector('#more')
const count = document.querySelector('#count')
let hits = []
let shown = 0

function showMore() {
  for (const p of hits.slice(shown, shown + PAGE)) {
    partCard({ title: p.t, lib: (LIB_NAMES[p.r] ?? p.r) + (p.p.startsWith('obsolete/') ? ' (obsolete)' : ''), moduleId: p.m, ref: p.user ? p.m : p.p }, results)
  }
  shown = Math.min(hits.length, shown + PAGE)
  more.hidden = shown >= hits.length
}

let timer
document.querySelector('#q').addEventListener('input', e => {
  clearTimeout(timer)
  timer = setTimeout(async () => {
    await ready
    queue.length = 0
    hits = search(e.target.value)
    shown = 0
    results.replaceChildren()
    count.textContent = e.target.value.trim() ? `${hits.length.toLocaleString()} part${hits.length === 1 ? '' : 's'}` : ''
    showMore()
  }, 200)
})
more.onclick = showMore

// --- the page -------------------------------------------------------------------

const gallery = document.querySelector('#gallery')
for (const ex of EXAMPLES) {
  const name = ex.file.split('/').pop().replace(/\.(fzz|json)$/, '')
  let cached
  const c = card({ ...ex, name, bytes: async () => (cached ??= await fetchBytes(ex.file)) }, gallery)
  c.el._draw = c.draw
  whenSeen.observe(c.el)
}

// Dropped files: parts (.fzpz, .fzp with its .svg drawings) join your parts;
// a sketch (.fzz, .fz, .json) is drawn, after any parts dropped with it.
const yours = document.querySelector('#yours-out')
async function openFiles(list) {
  const files = [...list]
  const partFiles = files.filter(f => /\.(fzpz|fzp|svg)$/i.test(f.name))
  const sketch = files.find(f => /\.(fzz|fz|json)$/i.test(f.name))
  const note = document.querySelector('#drop-note')
  note.textContent = ''
  if (partFiles.length) {
    try {
      const added = await addUserParts(partFiles)
      note.textContent = added.length ? `Added ${added.map(p => p.title).join(', ')} to your parts.` : ''
    } catch (e) {
      note.textContent = String(e.message ?? e)
    }
  }
  if (sketch) {
    const bytes = new Uint8Array(await sketch.arrayBuffer())
    yours.replaceChildren()
    card({ title: sketch.name, about: '', bytes: async () => bytes, name: sketch.name.replace(/\.\w+$/, ''), schematic: /\.(fzz|fz)$/i.test(sketch.name) }, yours).draw()
  }
}
const drop = document.querySelector('#drop')
drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('over') })
drop.addEventListener('dragleave', () => drop.classList.remove('over'))
drop.addEventListener('drop', e => { e.preventDefault(); drop.classList.remove('over'); openFiles(e.dataTransfer.files) })
document.querySelector('#file').addEventListener('change', e => openFiles(e.target.files))
