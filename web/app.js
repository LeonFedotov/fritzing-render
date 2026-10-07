// The web demo: loads the WebAssembly renderer and the parts index, fetches
// the parts a sketch names from GitHub (pinned commits) into the renderer's
// file system, and shows the result.

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

const status = document.querySelector('#status')
const say = text => { status.textContent = text }

// --- the renderer -----------------------------------------------------------

let renderer
const ready = (async () => {
  const instance = await qtLoad({
    qt: {
      entryFunction: window.fritzing_render_web_entry,
      containerElements: [document.querySelector('#qt')],
      onExit: () => {},
    },
  })
  renderer = instance
  const index = await (await fetch('parts-index.json')).json()
  buildLookup(index)
  say(`Ready: ${index.parts.length.toLocaleString()} parts from ${Object.keys(index.roots).length + new Set(index.parts.filter(p => p.z !== undefined).map(p => p.r)).size} libraries.`)
})().catch(e => {
  say('The renderer failed to load: ' + e)
  throw e
})

// --- parts --------------------------------------------------------------------

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
const loaded = new Set()

function write(path, bytes) {
  renderer.FS.mkdirTree(path.slice(0, path.lastIndexOf('/')))
  renderer.FS.writeFile(path, bytes)
}

async function fetchBytes(href) {
  const r = await fetch(href)
  if (!r.ok) throw new Error(`${r.status} for ${href}`)
  return new Uint8Array(await r.arrayBuffer())
}

// Fetches a part into /parts/<library>/..., laid out as vendor/ is: loose
// files from the library's repository, or the archive it ships in, unpacked.
async function loadPart(p) {
  if (p.z !== undefined) {
    const key = 'z' + p.z
    if (loaded.has(key)) return
    loaded.add(key)
    const folder = p.p.split('/')[0]
    const files = unzipSync(await fetchBytes(INDEX.archives[p.z]))
    for (const [name, bytes] of Object.entries(files)) {
      if (!name.endsWith('/')) write(`/parts/${p.r}/${folder}/${name.split('/').pop()}`, bytes)
    }
    return
  }
  const base = INDEX.roots[p.r].base
  await Promise.all([p.p, p.b, p.s].filter(Boolean).map(async path => {
    const key = `${p.r}/${path}`
    if (loaded.has(key)) return
    loaded.add(key)
    write(`/parts/${key}`, await fetchBytes(url(base, path)))
  }))
}

async function loadPartsFor(bytes) {
  const refs = JSON.parse(renderer.modules(bytes))
  const parts = refs.map(resolve).filter(Boolean)
  await Promise.all(parts.map(loadPart))
  return parts.length
}

// --- rendering ----------------------------------------------------------------

async function renderBytes(bytes, view, theme) {
  await ready
  await loadPartsFor(bytes)
  return JSON.parse(renderer.render(bytes, view, theme))
}

const FONTS = `<link rel="stylesheet" href="${new URL('fonts.css', location.href)}">`

// Each drawing in its own frame: part drawings reuse element ids, and the
// frame can load Fritzing's fonts for the text in them.
function frame(result) {
  const f = document.createElement('iframe')
  f.className = 'drawing'
  f.title = 'drawing'
  f.srcdoc = `<!doctype html><html><head>${FONTS}<style>html,body{margin:0;background:#fff}svg{display:block;width:100%;height:auto}</style></head><body>${result.svg}</body></html>`
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

// A card: a title, view and theme switches, the drawing, warnings, downloads.
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

// --- the page -------------------------------------------------------------------

const gallery = document.querySelector('#gallery')
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

for (const ex of EXAMPLES) {
  const name = ex.file.split('/').pop().replace(/\.(fzz|json)$/, '')
  let cached
  const c = card({ ...ex, name, bytes: async () => (cached ??= await fetchBytes(ex.file)) }, gallery)
  c.el._draw = c.draw
  seen.observe(c.el)
}

const yours = document.querySelector('#yours-out')
async function open(file) {
  const bytes = new Uint8Array(await file.arrayBuffer())
  const isSketch = /\.(fzz|fz)$/i.test(file.name)
  yours.replaceChildren()
  card({ title: file.name, about: '', bytes: async () => bytes, name: file.name.replace(/\.\w+$/, ''), schematic: isSketch }, yours).draw()
}
const drop = document.querySelector('#drop')
drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('over') })
drop.addEventListener('dragleave', () => drop.classList.remove('over'))
drop.addEventListener('drop', e => { e.preventDefault(); drop.classList.remove('over'); if (e.dataTransfer.files[0]) open(e.dataTransfer.files[0]) })
document.querySelector('#file').addEventListener('change', e => e.target.files[0] && open(e.target.files[0]))
