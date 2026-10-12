// A part's own page: its breadboard and schematic views, its files (the
// part file and each view's drawing, to look at and download, or all of them
// as a .fzpz), its connectors and its properties.
//
//   part.html?lib=<library>&p=<part file path>   a library part
//   part.html?user=<key>                          one of your parts

import { zipSync } from 'https://cdn.jsdelivr.net/npm/fflate@0.8.2/esm/browser.js'
import {
  INDEX, USER, copyLine, describe, download, frame, libName, listDir, loadPart, readFile, readFzp,
  ready, refOf, renderBytes, sketchOf, url, userEntry,
} from './lib.js'

const $ = s => document.querySelector(s)
const params = new URLSearchParams(location.search)
const VIEW_NAMES = { iconView: 'Icon', breadboardView: 'Breadboard', schematicView: 'Schematic', pcbView: 'PCB' }

function fail(message) {
  $('#title').textContent = 'Part not found'
  $('#sub').textContent = message
  $('#crumb').textContent = 'not found'
  for (const s of document.querySelectorAll('#part > section')) s.hidden = true
  $('#part .actions').hidden = true
}

const size = n => n < 1024 ? `${n} B` : `${(n / 1024).toFixed(1)} KB`
const fileName = path => path.split('/').pop()

// The part's files: [{role, path (as named in the part), bytes, href?}].
async function filesOf(entry, info) {
  const views = Object.entries(info.views)
  if (entry.user) {
    const u = USER.get(entry.key)
    return [{ role: 'Part file', path: u.fzp, bytes: u.files[u.fzp] },
      ...views.map(([v, image]) => ({ role: VIEW_NAMES[v] ?? v, path: image, bytes: u.files[image] })).filter(f => f.bytes)]
  }
  const fzp = { role: 'Part file', path: fileName(entry.p), bytes: readFile(`/parts/${entry.r}/${entry.p}`) }
  if (entry.z !== undefined) {
    // an archive, unpacked flat: svg.<view>.<file> beside the part file
    const dir = `/parts/${entry.r}/${entry.p.split('/')[0]}`
    const names = new Set(listDir(dir))
    return [fzp, ...views.map(([v, image]) => {
      const [folder, name] = [image.split('/')[0], fileName(image)]
      const found = [`svg.${folder}.${name}`, name].find(n => names.has(n))
      return found && { role: VIEW_NAMES[v] ?? v, path: found, bytes: readFile(`${dir}/${found}`) }
    }).filter(Boolean)]
  }
  // loose files: each view's drawing under the folder the index's paths show
  const base = INDEX.roots[entry.r].base
  const known = [[info.views.breadboardView, entry.b], [info.views.schematicView, entry.s]].find(([image, path]) => image && path?.endsWith(image))
  const prefix = known ? known[1].slice(0, -known[0].length) : `svg/${entry.p.split('/')[0]}/`
  fzp.href = url(base, entry.p)
  const out = [fzp]
  for (const [v, image] of views) {
    const path = prefix + image
    try {
      await loadPart(entry, [path])
    } catch {
      continue   // a view the library doesn't have the drawing for
    }
    out.push({ role: VIEW_NAMES[v] ?? v, path: fileName(image), bytes: readFile(`/parts/${entry.r}/${path}`), href: url(base, path), image })
  }
  return out
}

function fileTile(f, host) {
  const el = document.createElement('figure')
  el.className = 'file'
  const isSvg = /\.svg$/i.test(f.path)
  const blob = new Blob([f.bytes], { type: isSvg ? 'image/svg+xml' : 'application/xml' })
  el.innerHTML = `<div class="thumb"></div><figcaption><strong></strong><span class="hint"></span><code></code>
    <span class="row"><button class="link dl">Download</button><a class="link gh" target="_blank" rel="noopener" hidden>GitHub</a></span></figcaption>`
  if (isSvg) {
    const img = new Image()
    img.alt = `${f.role} drawing`
    img.src = URL.createObjectURL(blob)
    el.querySelector('.thumb').append(img)
  } else {
    el.querySelector('.thumb').innerHTML = '<span class="fzp">.fzp</span>'
  }
  el.querySelector('strong').textContent = f.role
  el.querySelector('.hint').textContent = size(f.bytes.length)
  el.querySelector('code').textContent = f.path
  el.querySelector('.dl').onclick = () => download(fileName(f.path), blob)
  if (f.href) {
    const a = el.querySelector('.gh')
    a.hidden = false
    a.href = f.href
  }
  host.append(el)
}

// The files as a .fzpz, the flat layout Fritzing imports: part.<name>.fzp
// beside svg.<view>.<file>.
function fzpz(files, info) {
  const zip = {}
  const stem = fileName(files[0].path).replace(/^part\./, '').replace(/\.fzp$/, '')
  zip[`part.${stem}.fzp`] = files[0].bytes
  for (const [v, image] of Object.entries(info.views)) {
    const f = files.find(x => x.role === (VIEW_NAMES[v] ?? v))
    if (!f) continue
    const folder = image.split('/')[0]
    const name = fileName(image)
    zip[`svg.${folder}.${name}`] = f.bytes
  }
  return zipSync(zip)
}

async function drawView(stage, ref, view, theme) {
  stage.innerHTML = '<p class="pending">Rendering…</p>'
  try {
    const r = await renderBytes(sketchOf(ref, 9), view, theme)
    if (r.error) throw new Error(r.error)
    stage.replaceChildren(frame(r))
  } catch (e) {
    stage.innerHTML = '<p class="error"></p>'
    stage.firstChild.textContent = String(e.message ?? e)
  }
}

function row(cells, host) {
  const tr = document.createElement('tr')
  for (const c of cells) {
    const td = document.createElement('td')
    if (c instanceof Node) td.append(c)
    else td.textContent = c
    tr.append(td)
  }
  host.append(tr)
}

async function main() {
  await ready
  let entry
  if (params.has('user')) {
    const u = USER.get(params.get('user'))
    if (!u) return fail('That part is not among your parts in this browser.')
    entry = userEntry(u)
  } else {
    entry = INDEX.parts.find(x => x.r === params.get('lib') && x.p === params.get('p'))
    if (!entry) return fail(`No part ${params.get('p') ?? ''} in ${params.get('lib') ?? 'the libraries'}.`)
    await loadPart(entry)
  }
  const ref = refOf(entry)
  const fzpBytes = entry.user ? USER.get(entry.key).files[USER.get(entry.key).fzp] : readFile(`/parts/${entry.r}/${entry.p}`)
  const info = fzpBytes && readFzp(fzpBytes)
  if (!info) return fail('Its part file could not be read.')

  document.title = `${info.title} · Parts`
  $('#title').textContent = info.title
  $('#crumb').textContent = info.title
  $('#sub').textContent = `${libName(entry)} · ${info.moduleId}`
  $('#desc').textContent = info.description
  $('#add').href = `editor.html?add=${encodeURIComponent(ref)}`
  $('#copy').onclick = e => copyLine(e.target, ref)

  // views
  let theme = 'fritzing'
  drawView($('#bb'), ref, 'breadboard', 'fritzing')
  drawView($('#sch'), ref, 'schematic', theme)
  for (const b of document.querySelectorAll('[data-theme]')) {
    b.onclick = () => {
      theme = b.dataset.theme
      document.querySelectorAll('[data-theme]').forEach(x => x.classList.toggle('on', x === b))
      drawView($('#sch'), ref, 'schematic', theme)
    }
  }

  // files
  const files = await filesOf(entry, info)
  for (const f of files) fileTile(f, $('#files'))
  $('#files-note').textContent = entry.user
    ? 'Kept in this browser.'
    : entry.z !== undefined
      ? `From ${fileName(decodeURIComponent(INDEX.archives[entry.z]))} in the ${libName(entry)} library, at a pinned commit.`
      : `From the ${libName(entry)} library on GitHub, at a pinned commit.`
  $('#fzpz').onclick = () => download(`${fileName(files[0].path).replace(/^part\./, '').replace(/\.fzp$/, '')}.fzpz`, new Blob([fzpz(files, info)], { type: 'application/zip' }))

  // connectors, with their breadboard positions
  let at = new Map()
  try {
    at = new Map((await describe(ref)).connectors.map(c => [c.id, c]))
  } catch { /* drawn without breadboard geometry */ }
  const tbody = $('#connectors tbody')
  $('#ncon').textContent = `(${info.connectors.length})`
  const bused = new Map()
  for (const bus of info.buses) for (const id of bus) bused.set(id, bus.filter(x => x !== id))
  for (const c of info.connectors) {
    const p = at.get(c.id)
    const joined = bused.get(c.id)
    const desc = c.description + (joined?.length ? ` (joined to ${joined.map(id => info.connectors.find(x => x.id === id)?.name || id).join(', ')})` : '')
    row([c.name, c.id, desc, p?.found ? `${p.x.toFixed(1)}, ${p.y.toFixed(1)}` : '—'], tbody)
  }

  // properties
  const props = $('#props tbody')
  const meta = [['module id', info.moduleId], ['library', libName(entry)], ['file', entry.user ? entry.p : `${entry.r}/${entry.p}`],
    ...info.properties, ['label', info.label], ['tags', info.tags.join(', ')], ['version', info.version], ['author', info.author], ['date', info.date]]
  for (const [k, v] of meta) if (v) row([k, v], props)
  if (info.url) {
    const a = document.createElement('a')
    a.href = info.url
    a.textContent = info.url
    a.rel = 'noopener'
    row(['url', a], props)
  }
}

main().catch(e => fail(String(e.message ?? e)))
