// What every page shares: the WebAssembly renderer, the parts index, the
// parts a sketch names fetched from GitHub (pinned commits) into the
// renderer's file system, your parts (kept in this browser, IndexedDB, and
// searched before the libraries), search, and drawing cards.

import { unzipSync } from 'https://cdn.jsdelivr.net/npm/fflate@0.8.2/esm/browser.js'

export const USER_ROOT = 'user-parts'   // /parts/user-parts: searched first by the renderer (src/wasm.cpp)

const status = document.querySelector('#status')
export const say = text => { if (status) status.textContent = text }

// --- the renderer -----------------------------------------------------------

let renderer
export let INDEX
export const ready = (async () => {
  renderer = await qtLoad({
    qt: {
      entryFunction: window.fritzing_render_web_entry,
      containerElements: [document.querySelector('#qt')],
      onExit: () => {},
    },
  })
  buildLookup(await (await fetch('parts-index.json')).json())
  await restoreUserParts()
  const libraries = new Set(INDEX.parts.map(p => p.r)).size
  say(`Ready: ${INDEX.parts.length.toLocaleString()} parts from ${libraries} libraries.`)
})().catch(e => {
  say('The renderer failed to load: ' + e)
  throw e
})

// --- library parts --------------------------------------------------------------

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
export const resolve = ref => byPath.get(ref) ?? byModule.get(ref) ?? byTitle.get(ref.toLowerCase())

export const url = (base, path) => base + path.split('/').map(encodeURIComponent).join('/')

function write(path, bytes) {
  renderer.FS.mkdirTree(path.slice(0, path.lastIndexOf('/')))
  renderer.FS.writeFile(path, bytes)
}

export function readFile(path) {
  return renderer.FS.analyzePath(path).exists ? renderer.FS.readFile(path) : null
}

export function listDir(path) {
  if (!renderer.FS.analyzePath(path).exists) return []
  return renderer.FS.readdir(path).filter(n => n !== '.' && n !== '..')
}

export async function fetchBytes(href) {
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
// files from the library's repository (`extra`: more of them, such as its
// PCB drawing), or the archive it ships in, unpacked.
export function loadPart(p, extra = []) {
  if (p.z !== undefined) {
    return once('z' + p.z, async () => {
      const folder = p.p.split('/')[0]
      for (const [name, bytes] of Object.entries(unzipSync(await fetchBytes(INDEX.archives[p.z])))) {
        if (!name.endsWith('/')) write(`/parts/${p.r}/${folder}/${name.split('/').pop()}`, bytes)
      }
    })
  }
  const base = INDEX.roots[p.r].base
  return Promise.all([p.p, p.b, p.s, ...extra].filter(Boolean).map(path =>
    once(`${p.r}/${path}`, async () => write(`/parts/${p.r}/${path}`, await fetchBytes(url(base, path))))))
}

async function loadPartsFor(bytes) {
  const refs = JSON.parse(renderer.modules(bytes))
  // your parts are already in the file system
  const parts = refs.filter(ref => !userPartFor(ref)).map(resolve).filter(Boolean)
  await Promise.all(parts.map(p => loadPart(p)))
}

// --- your parts -------------------------------------------------------------------
// Each kept as {key, title, moduleId, files: {path in its folder: bytes}} in
// IndexedDB, and written to /parts/user-parts/<key>/ for the renderer.

export const USER = new Map()   // key -> part
export const userPartFor = ref => [...USER.values()].find(u => u.moduleId === ref || u.title.toLowerCase() === ref.toLowerCase() || `${u.key}/${u.fzp}` === ref)

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

// A part file's details, read in the page.
export function readFzp(bytes) {
  const doc = new DOMParser().parseFromString(decoder.decode(bytes).replace(/[\x00-\x08\x0b\x0c\x0e-\x1f]/g, ''), 'application/xml')
  const root = doc.documentElement
  if (!root || root.nodeName !== 'module') return null
  const text = sel => root.querySelector(sel)?.textContent?.trim() ?? ''
  const html = text(':scope > description')
  return {
    moduleId: root.getAttribute('moduleId') ?? '',
    title: text(':scope > title') || '(untitled)',
    version: text(':scope > version'),
    author: text(':scope > author'),
    date: text(':scope > date'),
    label: text(':scope > label'),
    url: text(':scope > url'),
    description: html ? new DOMParser().parseFromString(html, 'text/html').body.textContent.trim() : '',
    tags: [...root.querySelectorAll(':scope > tags > tag')].map(t => t.textContent.trim()).filter(Boolean),
    properties: [...root.querySelectorAll(':scope > properties > property')].map(p => [p.getAttribute('name') ?? '', p.textContent.trim()]),
    views: Object.fromEntries([...root.querySelectorAll(':scope > views > *')]
      .map(v => [v.nodeName, v.querySelector('layers')?.getAttribute('image') ?? ''])
      .filter(([, image]) => image)),
    images: [...root.querySelectorAll('views layers[image]')].map(l => l.getAttribute('image')),
    connectors: [...root.querySelectorAll(':scope > connectors > connector')].map(c => ({
      id: c.getAttribute('id') ?? '',
      name: c.getAttribute('name') ?? '',
      type: c.getAttribute('type') ?? '',
      description: c.querySelector('description')?.textContent?.trim() ?? '',
    })),
    buses: [...root.querySelectorAll(':scope > buses > bus')].map(b => [...b.querySelectorAll('nodeMember')].map(m => m.getAttribute('connectorId'))),
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
      const info = fzp && readFzp(unzipped[fzp])
      if (!info) throw new Error(`${f.name} has no part file (.fzp) in it`)
      parts.push({ key: keyFor(f.name.replace(/\.fzpz$/i, '')), title: info.title, moduleId: info.moduleId, fzp, files: unzipped })
    } else if (/\.fzp$/i.test(f.name)) {
      const info = readFzp(bytes)
      if (!info) throw new Error(`${f.name} is not a Fritzing part file`)
      const files = { [f.name]: bytes }
      const missing = []
      for (const image of info.images) {
        const svg = svgs.get(image.split('/').pop())
        if (svg) files[image] = svg   // partlib finds <image> beside the .fzp
        else if (/^(breadboard|schematic)\//.test(image)) missing.push(image.split('/').pop())
      }
      parts.push({ key: keyFor(f.name.replace(/\.fzp$/i, '')), title: info.title, moduleId: info.moduleId, fzp: f.name, files, missing })
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

export async function addUserParts(files) {
  await ready
  const parts = await partsFrom(files)
  for (const part of parts) {
    installUserPart(part)
    await db.put({ key: part.key, title: part.title, moduleId: part.moduleId, fzp: part.fzp, files: part.files, missing: part.missing })
  }
  return parts
}

export async function removeUserPart(key) {
  USER.delete(key)
  removeTree(`/parts/${USER_ROOT}/${key}`)
  await db.del(key)
}

// --- rendering ----------------------------------------------------------------

export async function renderBytes(bytes, view, theme) {
  await ready
  await loadPartsFor(bytes)
  return JSON.parse(renderer.render(bytes, view, theme))
}

const encoder = new TextEncoder()
export const encodeSketch = sketch => encoder.encode(JSON.stringify(sketch))
export const sketchOf = (ref, margin = 6) => encodeSketch({ parts: [{ id: 'part', part: ref }], margin })

// A part's breadboard size and connectors (scene units, 90 per inch).
export async function describe(ref) {
  await ready
  await loadPartsFor(sketchOf(ref))
  const d = JSON.parse(renderer.describe(ref))
  if (d.error) throw new Error(d.error)
  return d
}

const FONTS = `<link rel="stylesheet" href="${new URL('fonts.css', location.href)}">`

// Each drawing in its own frame: part drawings reuse element ids, and the
// frame can load Fritzing's fonts for the text in them.
export function frame(result) {
  const f = document.createElement('iframe')
  f.className = 'drawing'
  f.title = 'drawing'
  f.srcdoc = `<!doctype html><html><head>${FONTS}<style>html,body{margin:0;background:#fff}svg{display:block;width:100%;height:100vh}</style></head><body>${result.svg}</body></html>`
  f.style.aspectRatio = `${result.width} / ${result.height}`
  return f
}

export function download(name, blob) {
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = name
  a.click()
  setTimeout(() => URL.revokeObjectURL(a.href), 1000)
}

export async function png(result, ppi = 300) {
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
const seen = new IntersectionObserver(entries => {
  for (const e of entries) {
    if (!e.isIntersecting) continue
    seen.unobserve(e.target)
    queue.push(e.target._draw)
    pump()
  }
}, { rootMargin: '200px' })

// Runs draw() once el comes into view, in turn with the others.
export function whenSeen(el, draw) {
  el._draw = draw
  seen.observe(el)
}

// Forgets the drawings waiting their turn (a new search replaces them).
export const clearQueue = () => { queue.length = 0 }

function showError(stage, e) {
  stage.innerHTML = '<p class="error"></p>'
  stage.firstChild.textContent = String(e.message ?? e)
}

// A sketch card: a title, view and theme switches, the drawing, warnings,
// downloads, and (for a JSON sketch) a link to edit it.
export function card({ title, about, bytes, name, edit, view = 'breadboard', theme = 'fritzing' }, host) {
  const el = document.createElement('article')
  el.className = 'card'
  el.innerHTML = `<header><h3></h3><p class="about"></p>
    <div class="controls">
      <div class="seg view"><button data-view="breadboard">Breadboard</button><button data-view="schematic">Schematic</button></div>
      <div class="seg theme"><button data-theme="fritzing">Fritzing</button><button data-theme="modern">Modern</button></div>
      <span class="spacer"></span>
      <a class="link edit" hidden>Edit</a><button class="link svg">SVG</button><button class="link png">PNG</button>
    </div></header>
    <div class="stage"><p class="pending">Waiting…</p></div><p class="warn"></p>`
  el.querySelector('h3').textContent = title
  el.querySelector('.about').textContent = about ?? ''
  if (edit) {
    const a = el.querySelector('.edit')
    a.hidden = false
    a.href = edit
  }
  host.append(el)
  const state = { view, theme, result: null }
  const views = el.querySelectorAll('[data-view]')
  const themes = el.querySelectorAll('[data-theme]')

  async function draw() {
    views.forEach(b => b.classList.toggle('on', b.dataset.view === state.view))
    themes.forEach(b => b.classList.toggle('on', b.dataset.theme === state.theme))
    el.querySelector('.theme').hidden = state.view !== 'schematic'
    const stage = el.querySelector('.stage')
    stage.innerHTML = '<p class="pending">Fetching parts and rendering…</p>'
    try {
      const r = await renderBytes(await bytes(), state.view, state.theme)
      if (r.error) throw new Error(r.error)
      state.result = r
      stage.replaceChildren(frame(r))
      el.querySelector('.warn').textContent = r.warnings.join(' · ')
    } catch (e) {
      showError(stage, e)
    }
  }
  views.forEach(b => b.onclick = () => { state.view = b.dataset.view; draw() })
  themes.forEach(b => b.onclick = () => { state.theme = b.dataset.theme; draw() })
  el.querySelector('.svg').onclick = () => state.result && download(`${name}.svg`, new Blob([state.result.svg], { type: 'image/svg+xml' }))
  el.querySelector('.png').onclick = async () => state.result && download(`${name}.png`, await png(state.result))
  return { el, draw }
}

// --- parts: names, links, cards ---------------------------------------------------

export const LIB_NAMES = {
  'fritzing-parts': 'Fritzing', 'adafruit-parts': 'Adafruit', 'sparkfun-parts': 'SparkFun', 'seeed-parts': 'Seeed Studio',
  'mgesteiro-parts': 'mgesteiro', 'dip-ic-parts': 'DIP ICs', 'elegoo-parts': 'Elegoo', 'mkjanke-parts': 'mkjanke',
  'community-parts': 'community', 'fritzing-parts-extra': 'fritzing-parts-extra', [USER_ROOT]: 'your parts',
}
export const libName = p => (LIB_NAMES[p.r] ?? p.r) + (p.p.startsWith('obsolete/') ? ' (obsolete)' : '')

// What a sketch names a part by: your part's module id, a library part's path.
export const refOf = p => p.user ? p.m : p.p

// The part's own page.
export const partHref = p => p.user ? `part.html?user=${encodeURIComponent(p.key)}` : `part.html?lib=${encodeURIComponent(p.r)}&p=${encodeURIComponent(p.p)}`

export const userEntry = u => ({ t: u.title, m: u.moduleId, r: USER_ROOT, p: `${u.key}/${u.fzp}`, key: u.key, user: true })

export async function copyLine(button, ref) {
  await navigator.clipboard?.writeText(JSON.stringify({ id: 'p1', part: ref, x: 0, y: 0 }))
  const was = button.textContent
  button.textContent = 'Copied'
  setTimeout(() => { button.textContent = was }, 1500)
}

// A part card: its preview (rendered when it comes into view), title (a link
// to its page), library, module id, and actions.
export function partCard(p, host, { onRemove, missing } = {}) {
  const el = document.createElement('article')
  el.className = 'part'
  el.innerHTML = `<a class="stage small"><p class="pending">…</p></a>
    <div class="meta"><h4><a></a></h4><p class="lib"></p><code class="mid" title="module id"></code>
      <p class="warn"></p>
      <div class="actions"><a class="link details">Details</a><a class="link add">Add to editor</a><button class="link copy">Copy</button><button class="link remove" hidden>Remove</button></div></div>`
  const href = partHref(p)
  el.querySelector('h4 a').textContent = p.t
  for (const a of el.querySelectorAll('h4 a, .stage, .details')) a.href = href
  el.querySelector('.add').href = `editor.html?add=${encodeURIComponent(refOf(p))}`
  el.querySelector('.lib').textContent = libName(p)
  el.querySelector('.mid').textContent = p.m
  if (missing?.length) el.querySelector('.warn').textContent = `Drop its drawings too: ${missing.join(', ')}`
  el.querySelector('.copy').onclick = e => copyLine(e.target, refOf(p))
  el.querySelector('.copy').title = 'Copy a line for a JSON sketch'
  if (onRemove) {
    const b = el.querySelector('.remove')
    b.hidden = false
    b.onclick = onRemove
  }
  host.append(el)
  whenSeen(el, async () => {
    const stage = el.querySelector('.stage')
    try {
      const r = await renderBytes(sketchOf(refOf(p)), 'breadboard', 'fritzing')
      if (r.error) throw new Error(r.error)
      stage.replaceChildren(frame(r))
    } catch (e) {
      showError(stage, e)
    }
  })
  return el
}

// --- search -------------------------------------------------------------------------
// Every query word must appear in a part's title, module id, family, tags,
// library or file name; title matches rank first (partlib::search).

let haystacks

export function search(query) {
  const words = query.toLowerCase().split(/\s+/).filter(Boolean)
  if (!words.length) return []
  haystacks ??= INDEX.parts.map(p => [p, `${p.t} ${p.m} ${p.f ?? ''} ${p.g ?? ''} ${LIB_NAMES[p.r] ?? p.r} ${p.p}`.toLowerCase()])
  const mine = [...USER.values()].map(u => [userEntry(u), `${u.title} ${u.moduleId} your parts`.toLowerCase()])
  const hits = []
  for (const [p, hay] of [...mine, ...haystacks]) {
    if (!words.every(w => hay.includes(w))) continue
    const title = p.t.toLowerCase()
    const score = words.filter(w => title.includes(w)).length * 10 + (p.user ? 5 : 0) + (p.r === 'fritzing-parts' && p.p.startsWith('core/') ? 2 : 0) - (p.p.startsWith('obsolete/') ? 8 : 0)
    hits.push([score, p])
  }
  return hits.sort((a, b) => b[0] - a[0]).map(([, p]) => p)
}

// --- dropping files ----------------------------------------------------------------

// Wires a drop zone (a <label> holding a file input) to onFiles.
export function dropZone(zone, onFiles) {
  zone.addEventListener('dragover', e => { e.preventDefault(); zone.classList.add('over') })
  zone.addEventListener('dragleave', () => zone.classList.remove('over'))
  zone.addEventListener('drop', e => { e.preventDefault(); zone.classList.remove('over'); onFiles([...e.dataTransfer.files]) })
  zone.querySelector('input[type=file]').addEventListener('change', e => onFiles([...e.target.files]))
}

export const isPartFile = f => /\.(fzpz|fzp|svg)$/i.test(f.name)
