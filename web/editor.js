// The sketch editor: a JSON sketch (src/sketch.h) on a canvas. Parts come
// from the catalogue search; drag a part to move it, drag from one pin to
// another to wire them. Each part is drawn by the renderer on its own, so
// the canvas shows Fritzing's drawings; "Render" draws the whole sketch,
// breadboard or schematic. The sketch is kept in this browser between visits.
//
//   editor.html?add=<part ref>        adds a part to the sketch
//   editor.html?open=<sketch.json>    opens a sketch (an example, say)

import { card, describe, download, encodeSketch, fetchBytes, libName, partHref, ready, refOf, renderBytes, resolve, search, userPartFor, userEntry } from './lib.js'

const $ = s => document.querySelector(s)
const SVG = 'http://www.w3.org/2000/svg'
const STORE = 'fritzing-render:editor'
const GRID = 9         // 0.1 in, scene units
const SNAP = 4.5
// Fritzing's breadboard wire colors (resources/ratsnestcolors.xml)
const COLORS = { blue: '#418dd9', red: '#cc1414', black: '#404040', yellow: '#ffe24d', green: '#47cc79', grey: '#999999', white: '#ffffff', orange: '#ff7033' }

const el = (tag, attrs = {}, parent) => {
  const e = document.createElementNS(SVG, tag)
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v)
  parent?.append(e)
  return e
}

// --- the sketch and its history -------------------------------------------------

function stored() {
  try {
    const s = JSON.parse(localStorage.getItem(STORE))
    return Array.isArray(s?.parts) ? { wires: [], ...s } : null
  } catch {
    return null
  }
}

let sketch = stored() ?? { parts: [], wires: [] }
let selection = null   // {part: id} or {wire: index}
let color = 'blue'
const undoStack = []

function persist() {
  try {
    localStorage.setItem(STORE, JSON.stringify(sketch))
  } catch { /* private window: the sketch lasts until the page closes */ }
}

// Every change goes through here, so it can be undone.
function change(mutate) {
  undoStack.push(JSON.stringify(sketch))
  if (undoStack.length > 200) undoStack.shift()
  mutate()
  persist()
  draw()
}

function undo() {
  if (!undoStack.length) return
  sketch = JSON.parse(undoStack.pop())
  selection = null
  persist()
  draw()
}

const partById = id => sketch.parts.find(p => p.id === id)

// --- parts as drawn ---------------------------------------------------------------
// Each part rendered alone (margin 0), with its size and connectors.

const looks = new Map()   // part key -> {state: 'loading' | 'ready' | 'error', ...}
const keyOf = spec => spec.part ?? JSON.stringify(spec.generic)

// render.cpp's genericPart: header pins 0.1 in apart along the bottom.
function genericConnectors(g, width, height) {
  const n = g.pins.length
  const first = (width - GRID * (n - 1)) / 2
  return g.pins.map((name, i) => ({ id: `connector${i}`, name, x: first + i * GRID, y: height, found: true }))
}

function look(spec) {
  const key = keyOf(spec)
  if (!looks.has(key)) {
    looks.set(key, { state: 'loading' })
    ;(async () => {
      const one = { parts: [{ id: 'p', ...(spec.part ? { part: spec.part } : { generic: spec.generic }) }], margin: 0 }
      const r = await renderBytes(encodeSketch(one), 'breadboard', 'fritzing')
      if (r.error) throw new Error(r.error)
      const box = { left: r.left * 90, top: r.top * 90, width: r.width * 90, height: r.height * 90 }
      const d = spec.part ? await describe(spec.part) : { title: spec.generic.title, ...box }
      const connectors = spec.part ? d.connectors.filter(c => c.found) : genericConnectors(spec.generic, box.width, box.height)
      const href = URL.createObjectURL(new Blob([r.svg], { type: 'image/svg+xml' }))
      looks.set(key, { state: 'ready', title: d.title, width: d.width, height: d.height, connectors, href, box })
    })().catch(e => looks.set(key, { state: 'error', error: String(e.message ?? e) })).finally(draw)
  }
  return looks.get(key)
}

// Part coordinates to the scene, as render.cpp places a part: rotated about its centre.
function toScene(spec, lk, [x, y]) {
  const cx = lk.width / 2
  const cy = lk.height / 2
  const a = ((spec.rotate ?? 0) * Math.PI) / 180
  const dx = x - cx
  const dy = y - cy
  return [(spec.x ?? 0) + cx + dx * Math.cos(a) - dy * Math.sin(a), (spec.y ?? 0) + cy + dx * Math.sin(a) + dy * Math.cos(a)]
}

// A connector by id, else by name (fzp::findConnector).
function connector(lk, ref) {
  const r = ref.toLowerCase()
  return lk.connectors.find(c => c.id.toLowerCase() === r) ?? lk.connectors.find(c => c.name.toLowerCase() === r)
}

// What a wire names a pin by: its name when no other pin shares it, else its id.
function pinRef(lk, c) {
  return lk.connectors.filter(x => x.name.toLowerCase() === c.name.toLowerCase()).length === 1 && c.name ? c.name : c.id
}

// A wire end in the scene: "<part>.<connector>", {part, at}, or [x, y].
function endAt(end) {
  if (Array.isArray(end)) return end
  if (typeof end === 'object' && end) {
    const spec = partById(end.part)
    const lk = spec && looks.get(keyOf(spec))
    return lk?.state === 'ready' ? toScene(spec, lk, end.at) : null
  }
  const dot = String(end).indexOf('.')
  const spec = partById(String(end).slice(0, dot))
  const lk = spec && looks.get(keyOf(spec))
  if (lk?.state !== 'ready') return null
  const c = connector(lk, String(end).slice(dot + 1))
  return c ? toScene(spec, lk, [c.x, c.y]) : null
}

const wireTouches = (w, id) => [w.from, w.to].some(e => (typeof e === 'string' && e.startsWith(id + '.')) || e?.part === id)

// --- the canvas ---------------------------------------------------------------------

const canvas = $('#canvas')
const defs = el('defs', {}, canvas)
const pattern = el('pattern', { id: 'grid', width: GRID, height: GRID, patternUnits: 'userSpaceOnUse' }, defs)
el('circle', { cx: 0, cy: 0, r: 0.55, fill: '#cbd5e1' }, pattern)
const backdrop = el('rect', { fill: 'url(#grid)' }, canvas)
const partsLayer = el('g', {}, canvas)
const wiresLayer = el('g', {}, canvas)
const pinsLayer = el('g', {}, canvas)
const temp = el('line', { class: 'temp', 'stroke-width': 2, 'stroke-linecap': 'round', visibility: 'hidden' }, canvas)

const view = { x: -40, y: -40, scale: 2 }   // the scene point at the top-left, pixels per scene unit

function applyView() {
  const w = canvas.clientWidth / view.scale
  const h = canvas.clientHeight / view.scale
  canvas.setAttribute('viewBox', `${view.x} ${view.y} ${w} ${h}`)
  for (const [k, v] of Object.entries({ x: view.x, y: view.y, width: w, height: h })) backdrop.setAttribute(k, v)
}
new ResizeObserver(applyView).observe(canvas)

function scenePoint(e) {
  const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(canvas.getScreenCTM().inverse())
  return [p.x, p.y]
}

function fit() {
  const boxes = sketch.parts.map(spec => {
    const lk = looks.get(keyOf(spec))
    return lk?.state === 'ready' ? [spec.x ?? 0, spec.y ?? 0, lk.width, lk.height] : [spec.x ?? 0, spec.y ?? 0, 60, 30]
  })
  if (!boxes.length) {
    Object.assign(view, { x: -40, y: -40, scale: 2 })
    return applyView()
  }
  const x0 = Math.min(...boxes.map(b => b[0])) - 30
  const y0 = Math.min(...boxes.map(b => b[1])) - 30
  const x1 = Math.max(...boxes.map(b => b[0] + Math.max(b[2], b[3]))) + 30
  const y1 = Math.max(...boxes.map(b => b[1] + Math.max(b[2], b[3]))) + 30
  view.scale = Math.min(canvas.clientWidth / (x1 - x0), canvas.clientHeight / (y1 - y0), 6)
  view.x = x0
  view.y = y0
  applyView()
}

function draw() {
  partsLayer.replaceChildren()
  wiresLayer.replaceChildren()
  pinsLayer.replaceChildren()
  $('#empty').hidden = sketch.parts.length > 0
  for (const spec of sketch.parts) {
    const lk = look(spec)
    const selected = selection?.part === spec.id
    const g = el('g', { class: 'part' + (selected ? ' selected' : ''), 'data-id': spec.id }, partsLayer)
    const label = spec.label || spec.id
    if (lk.state !== 'ready') {
      const failed = lk.state === 'error'
      el('rect', { x: spec.x ?? 0, y: spec.y ?? 0, width: 80, height: 30, rx: 3, class: failed ? 'placeholder failed' : 'placeholder' }, g)
      const t = el('text', { x: (spec.x ?? 0) + 6, y: (spec.y ?? 0) + 18, class: 'note' }, g)
      t.textContent = failed ? `${spec.id}: ${lk.error}`.slice(0, 60) : `${spec.id}: loading…`
      continue
    }
    g.setAttribute('transform', `translate(${spec.x ?? 0} ${spec.y ?? 0}) rotate(${spec.rotate ?? 0} ${lk.width / 2} ${lk.height / 2})`)
    el('image', { href: lk.href, x: lk.box.left, y: lk.box.top, width: lk.box.width, height: lk.box.height }, g)
    el('rect', { class: 'outline', x: lk.box.left - 2, y: lk.box.top - 2, width: lk.box.width + 4, height: lk.box.height + 4, rx: 2 }, g)
    const t = el('text', { class: 'tag', x: spec.x ?? 0, y: (spec.y ?? 0) - 4 }, partsLayer)
    t.textContent = label
    for (const c of lk.connectors) {
      const [x, y] = toScene(spec, lk, [c.x, c.y])
      const pin = el('circle', { class: 'pin', cx: x, cy: y, r: 3.2, 'data-end': `${spec.id}.${pinRef(lk, c)}` }, pinsLayer)
      el('title', {}, pin).textContent = `${spec.id}.${c.name || c.id}${c.description ? ` (${c.description})` : ''}`
    }
  }
  sketch.wires.forEach((w, i) => {
    const pts = [endAt(w.from), ...(w.via ?? []), endAt(w.to)]
    if (pts.some(p => !p)) return
    const points = pts.map(p => p.join(',')).join(' ')
    const stroke = COLORS[w.color ?? 'blue'] ?? w.color ?? COLORS.blue
    const g = el('g', { class: 'wire' + (selection?.wire === i ? ' selected' : ''), 'data-index': i }, wiresLayer)
    el('polyline', { points, class: 'hit' }, g)
    el('polyline', { points, class: 'shadow' }, g)
    el('polyline', { points, stroke, class: 'line' }, g)
  })
  inspector()
  $('#rotate').disabled = !selection?.part
  $('#delete').disabled = !selection
  $('#undo').disabled = !undoStack.length
}

// --- editing ------------------------------------------------------------------------

function uniqueId(title) {
  // the title's first two words: arduino_nano, red_led
  const base = title.toLowerCase().replace(/[^a-z0-9]+/g, ' ').trim().split(' ').slice(0, 2).join('_') || 'part'
  let id = base
  for (let n = 2; partById(id); n++) id = `${base}${n}`
  return id
}

async function addPart(ref) {
  await ready
  const p = userPartFor(ref) ? null : resolve(ref)
  const title = userPartFor(ref)?.title ?? p?.t ?? ref
  const w = canvas.clientWidth / view.scale
  const h = canvas.clientHeight / view.scale
  const x = Math.round((view.x + w / 2 - 40) / SNAP) * SNAP
  const y = Math.round((view.y + h / 2 - 40) / SNAP) * SNAP
  const id = uniqueId(title)
  change(() => sketch.parts.push({ id, part: ref, x, y }))
  selection = { part: id }
  draw()
}

function deleteSelection() {
  if (!selection) return
  change(() => {
    if (selection.part) {
      sketch.wires = sketch.wires.filter(w => !wireTouches(w, selection.part))
      sketch.parts = sketch.parts.filter(p => p.id !== selection.part)
    } else {
      sketch.wires.splice(selection.wire, 1)
    }
    selection = null
  })
}

function rotateSelection() {
  const spec = selection?.part && partById(selection.part)
  if (!spec) return
  change(() => {
    spec.rotate = ((spec.rotate ?? 0) + 90) % 360
    for (const w of sketch.wires) if (wireTouches(w, spec.id)) delete w.via
  })
}

function renamePart(spec, id) {
  id = id.trim().replace(/[.\s]+/g, '_')
  if (!id || id === spec.id || partById(id)) return
  const old = spec.id
  change(() => {
    for (const w of sketch.wires) {
      for (const k of ['from', 'to']) {
        if (typeof w[k] === 'string' && w[k].startsWith(old + '.')) w[k] = id + w[k].slice(old.length)
        else if (w[k]?.part === old) w[k].part = id
      }
    }
    spec.id = id
    selection = { part: id }
  })
}

// The selection's details: a part's id, label and page; a wire's color.
function inspector() {
  const box = $('#inspector')
  const spec = selection?.part && partById(selection.part)
  const wire = selection?.wire !== undefined && sketch.wires[selection.wire]
  box.hidden = !spec && !wire
  box.replaceChildren()
  if (spec) {
    const lk = looks.get(keyOf(spec))
    box.innerHTML = `<p class="title"></p><label>Id <input class="id" spellcheck="false"></label>
      <label>Label <input class="label" placeholder="drawn above the part"></label>
      <p class="row"><a class="link details">Details</a><span class="hint pins"></span></p>`
    box.querySelector('.title').textContent = lk?.title ?? spec.part ?? spec.generic?.title
    box.querySelector('.id').value = spec.id
    box.querySelector('.id').onchange = e => renamePart(spec, e.target.value)
    box.querySelector('.label').value = spec.label ?? ''
    box.querySelector('.label').onchange = e => change(() => {
      if (e.target.value.trim()) spec.label = e.target.value
      else delete spec.label
    })
    const p = spec.part && (userPartFor(spec.part) ? userEntry(userPartFor(spec.part)) : resolve(spec.part))
    const details = box.querySelector('.details')
    if (p) details.href = partHref(p)
    else details.hidden = true
    if (lk?.connectors) box.querySelector('.pins').textContent = `${lk.connectors.length} pins`
  } else if (wire) {
    box.innerHTML = '<p class="title">Wire</p><p class="hint ends"></p><div class="swatches"></div>'
    const name = e => typeof e === 'string' ? e : Array.isArray(e) ? `(${e.join(', ')})` : `${e.part}@(${e.at.join(', ')})`
    box.querySelector('.ends').textContent = `${name(wire.from)} → ${name(wire.to)}`
    swatches(box.querySelector('.swatches'), wire.color ?? 'blue', c => change(() => { wire.color = c }))
  }
}

function swatches(host, current, onPick) {
  host.replaceChildren()
  for (const [name, hex] of Object.entries(COLORS)) {
    const b = document.createElement('button')
    b.className = 'swatch' + (name === current ? ' on' : '')
    b.style.background = hex
    b.title = name
    b.setAttribute('aria-label', `${name} wire`)
    b.onclick = () => onPick(name)
    host.append(b)
  }
}

function pickColor(c) {
  color = c
  swatches($('#colors'), color, pickColor)
}
pickColor('blue')

// --- pointer and keys ----------------------------------------------------------------

let drag = null

canvas.addEventListener('pointerdown', e => {
  if (e.button !== 0) return
  const at = scenePoint(e)
  const pin = e.target.closest('.pin')
  const wire = e.target.closest('.wire')
  const part = e.target.closest('.part')
  canvas.setPointerCapture(e.pointerId)
  if (pin) {
    const start = [Number(pin.getAttribute('cx')), Number(pin.getAttribute('cy'))]
    drag = { kind: 'wire', from: pin.dataset.end, start }
    temp.setAttribute('stroke', COLORS[color])
    for (const [k, v] of Object.entries({ x1: start[0], y1: start[1], x2: start[0], y2: start[1] })) temp.setAttribute(k, v)
    temp.setAttribute('visibility', 'visible')
    canvas.classList.add('wiring')
  } else if (wire) {
    selection = { wire: Number(wire.dataset.index) }
    draw()
  } else if (part) {
    const spec = partById(part.dataset.id)
    selection = { part: spec.id }
    drag = { kind: 'move', spec, at, x: spec.x ?? 0, y: spec.y ?? 0, moved: false }
    draw()
  } else {
    selection = null
    drag = { kind: 'pan', client: [e.clientX, e.clientY], view: { ...view } }
    draw()
  }
})

canvas.addEventListener('pointermove', e => {
  if (!drag) return
  if (drag.kind === 'wire') {
    const [x, y] = scenePoint(e)
    temp.setAttribute('x2', x)
    temp.setAttribute('y2', y)
  } else if (drag.kind === 'move') {
    const [x, y] = scenePoint(e)
    const nx = Math.round((drag.x + x - drag.at[0]) / SNAP) * SNAP
    const ny = Math.round((drag.y + y - drag.at[1]) / SNAP) * SNAP
    if (nx === drag.spec.x && ny === drag.spec.y) return
    if (!drag.moved) {
      // one undo step per move; bends in its wires no longer fit
      undoStack.push(JSON.stringify(sketch))
      for (const w of sketch.wires) if (wireTouches(w, drag.spec.id)) delete w.via
      drag.moved = true
    }
    drag.spec.x = nx
    drag.spec.y = ny
    draw()
  } else {
    view.x = drag.view.x - (e.clientX - drag.client[0]) / view.scale
    view.y = drag.view.y - (e.clientY - drag.client[1]) / view.scale
    applyView()
  }
})

canvas.addEventListener('pointerup', e => {
  if (drag?.kind === 'wire') {
    temp.setAttribute('visibility', 'hidden')
    canvas.classList.remove('wiring')
    const target = document.elementFromPoint(e.clientX, e.clientY)?.closest('.pin')
    const to = target?.dataset.end
    if (to && to !== drag.from) change(() => sketch.wires.push({ from: drag.from, to, color }))
  } else if (drag?.kind === 'move' && drag.moved) {
    persist()
  }
  drag = null
})

canvas.addEventListener('wheel', e => {
  e.preventDefault()
  const [x, y] = scenePoint(e)
  const scale = Math.min(12, Math.max(0.25, view.scale * Math.exp(-e.deltaY * 0.0015)))
  view.x = x - (x - view.x) * (view.scale / scale)
  view.y = y - (y - view.y) * (view.scale / scale)
  view.scale = scale
  applyView()
}, { passive: false })

document.addEventListener('keydown', e => {
  if (e.target.closest('input, textarea')) return
  if ((e.key === 'z' || e.key === 'Z') && (e.ctrlKey || e.metaKey)) {
    e.preventDefault()
    undo()
  } else if (e.key === 'Delete' || e.key === 'Backspace') {
    e.preventDefault()
    deleteSelection()
  } else if (e.key === 'r' || e.key === 'R') {
    rotateSelection()
  } else if (e.key === 'Escape') {
    selection = null
    draw()
  }
})

// --- the toolbar ----------------------------------------------------------------------

$('#new').onclick = () => change(() => {
  sketch = { parts: [], wires: [] }
  selection = null
  $('#out').replaceChildren()
})
$('#rotate').onclick = rotateSelection
$('#delete').onclick = deleteSelection
$('#undo').onclick = undo
$('#fit').onclick = fit
$('#save').onclick = () => download('sketch.json', new Blob([JSON.stringify(sketch, null, 2) + '\n'], { type: 'application/json' }))

function openSketch(text) {
  const s = JSON.parse(text)
  if (!Array.isArray(s.parts)) throw new Error('not a JSON sketch: it has no "parts"')
  change(() => {
    sketch = { wires: [], ...s }
    selection = null
  })
  // fit once its parts are drawn
  Promise.all(sketch.parts.map(p => waitFor(keyOf(p)))).then(fit)
}

function waitFor(key) {
  return new Promise(res => {
    const check = () => looks.get(key)?.state === 'loading' ? setTimeout(check, 100) : res()
    check()
  })
}

$('#open').onchange = async e => {
  const f = e.target.files[0]
  if (!f) return
  try {
    openSketch(await f.text())
  } catch (err) {
    $('#empty').textContent = String(err.message ?? err)
  }
  e.target.value = ''
}

$('#render').onclick = () => {
  const out = $('#out')
  out.replaceChildren()
  const snapshot = encodeSketch(sketch)
  const c = card({ title: 'Your sketch', about: 'As fritzing-render draws it. The schematic is laid out from the wiring.', bytes: async () => snapshot, name: 'sketch' }, out)
  c.draw()
  out.scrollIntoView({ behavior: 'smooth', block: 'start' })
}

// --- finding parts ----------------------------------------------------------------------

const results = $('#results')
let timer
$('#q').addEventListener('input', e => {
  clearTimeout(timer)
  timer = setTimeout(async () => {
    await ready
    results.replaceChildren()
    const hits = search(e.target.value).slice(0, 60)
    $('#pick-hint').hidden = hits.length > 0
    for (const p of hits) {
      const li = document.createElement('li')
      li.innerHTML = '<button><strong></strong><span></span></button>'
      li.querySelector('strong').textContent = p.t
      li.querySelector('span').textContent = libName(p)
      li.querySelector('button').onclick = () => addPart(refOf(p))
      results.append(li)
    }
  }, 150)
})

// --- start ------------------------------------------------------------------------------

applyView()
draw()
const params = new URLSearchParams(location.search)
const start = (async () => {
  if (params.has('open')) {
    openSketch(new TextDecoder().decode(await fetchBytes(params.get('open'))))
  } else {
    await ready
    Promise.all(sketch.parts.map(p => waitFor(keyOf(p)))).then(fit)
  }
  if (params.has('add')) await addPart(params.get('add'))
  history.replaceState(null, '', location.pathname)
})()
start.catch(e => { $('#empty').hidden = false; $('#empty').textContent = String(e.message ?? e) })
