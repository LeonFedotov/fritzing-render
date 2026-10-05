import { test } from 'node:test'
import assert from 'node:assert/strict'
import { existsSync } from 'node:fs'
import { join, resolve } from 'node:path'

import {
  binaryPath,
  describePart,
  formatPart,
  formatSearch,
  parseWarnings,
  renderArgs,
  renderDiagram,
  renderSketchFile,
  renderSummary,
  searchParts,
  type PartInfo,
} from '../src/cli.ts'

const repoRoot = resolve(import.meta.dirname, '../..')
const bin = binaryPath(process.env, repoRoot)
const haveBinary = existsSync(bin) && existsSync(join(repoRoot, 'vendor/fritzing-parts'))

test('binaryPath prefers FRITZING_RENDER_BIN, else the CMake build', () => {
  assert.equal(binaryPath({ FRITZING_RENDER_BIN: '/opt/fr' }, '/repo'), '/opt/fr')
  assert.equal(binaryPath({}, '/repo'), '/repo/build/fritzing-render')
})

test('renderArgs writes both files and passes ppi and transparency', () => {
  assert.deepEqual(renderArgs({ sketchFile: 's.json', svgFile: 'o.svg', pngFile: 'o.png', ppi: 200, transparent: false }), [
    'render', 's.json', '-o', 'o.svg', '--png', 'o.png', '--ppi', '200',
  ])
  assert.deepEqual(renderArgs({ sketchFile: 's.json', svgFile: 'o.svg', pngFile: 'o.png', ppi: 150, transparent: true }).at(-1), '--transparent')
  assert.deepEqual(renderArgs({ sketchFile: 's.fzz', svgFile: 'o.svg', pngFile: 'o.png', ppi: 150, transparent: false, view: 'schematic' }).slice(-2), ['--view', 'schematic'])
})

test('parseWarnings keeps the CLI\'s warning lines', () => {
  assert.deepEqual(parseWarnings('warning: 1 note left out\nsomething else\nwarning: J1 left out\n'), ['1 note left out', 'J1 left out'])
  assert.deepEqual(parseWarnings(''), [])
})

test('renderSummary lists saved files and warnings', () => {
  const text = renderSummary('a.fzz', { png: Buffer.alloc(0), svg: '', warnings: ['1 note left out: notes are not drawn'] }, ['/tmp/a.png'])
  assert.equal(text, 'Rendered a.fzz. Saved: /tmp/a.png\nwarning: 1 note left out: notes are not drawn')
})

test('formatSearch shows the title and the ref to use in a sketch', () => {
  const text = formatSearch([
    { title: 'Arduino Nano (Rev3.0)', ref: 'core/Arduino Nano3(fix).fzp', family: 'microcontroller board (arduino)', moduleId: 'm', path: '/x', tags: [] },
  ])
  assert.match(text, /Arduino Nano \(Rev3\.0\)/)
  assert.match(text, /part: "core\/Arduino Nano3\(fix\)\.fzp"/)
  assert.equal(formatSearch([]), 'No parts matched.')
})

test('formatPart lists connectors with names and positions', () => {
  const info: PartInfo = {
    title: 'Test Part', ref: 'core/testpart.fzp', moduleId: 'T', path: '/x', svg: '/y', width: 36, height: 18,
    connectors: [
      { id: 'connector0', name: 'IN', description: 'input pin', x: 4.5, y: 14.4, found: true },
      { id: 'connector1', name: 'OUT', description: '', x: 0, y: 0, found: false },
    ],
  }
  const text = formatPart(info)
  assert.match(text, /36 x 18/)
  assert.match(text, /IN\s+connector0\s+\(4\.5, 14\.4\)\s+input pin/)
  assert.match(text, /OUT\s+connector1\s+no breadboard geometry/)
})

test('search, describe and render through the real binary', { skip: !haveBinary && 'build and fetch-vendor first' }, async () => {
  const hits = await searchParts(bin, 'arduino nano', 3)
  assert.ok(hits.some(h => h.ref === 'core/Arduino Nano3(fix).fzp'))

  const nano = await describePart(bin, 'core/Arduino Nano3(fix).fzp')
  assert.equal(nano.title, 'Arduino Nano (Rev3.0)')
  assert.ok(nano.connectors.some(c => c.name === 'D2'))

  const out = await renderDiagram(bin, {
    parts: [
      { id: 'mcu', part: 'core/Arduino Nano3(fix).fzp', x: 0, y: 0 },
      { id: 'r1', part: 'core/resistor.fzp', x: 120, y: 40 },
    ],
    wires: [{ from: 'mcu.D2', to: 'r1.connector0', color: 'green' }],
  }, { ppi: 100 })
  assert.equal(out.png.subarray(1, 4).toString(), 'PNG')
  assert.match(out.svg, /<svg/)
})

test('wire ends can be spots on a part or scene points', { skip: !haveBinary && 'build first' }, async () => {
  const out = await renderDiagram(bin, {
    parts: [{ id: 'mcu', part: 'core/Arduino Nano3(fix).fzp', x: 0, y: 0 }],
    wires: [{ from: { part: 'mcu', at: [10, 10] }, to: [200, 10], color: '#123456' }],
  }, { ppi: 60 })
  assert.match(out.svg, /stroke='#123456'/)
})

test('render errors come back as messages, not crashes', { skip: !haveBinary && 'build first' }, async () => {
  await assert.rejects(
    renderDiagram(bin, { parts: [{ id: 'mcu', part: 'core/Arduino Nano3(fix).fzp' }], wires: [{ from: 'mcu.D99', to: 'mcu.D2' }] }, {}),
    /no connector "D99"/,
  )
})

test('renders a Fritzing .fzz file, with its bundled parts', { skip: !existsSync(bin) && 'build first' }, async () => {
  const out = await renderSketchFile(bin, join(repoRoot, 'tests/fixtures/bundled.fzz'), { ppi: 90 })
  assert.equal(out.png.subarray(1, 4).toString(), 'PNG')
  assert.match(out.svg, /<svg/)
  assert.deepEqual(out.warnings, [])
})

test('sketch file errors come back as messages', { skip: !existsSync(bin) && 'build first' }, async () => {
  await assert.rejects(renderSketchFile(bin, join(repoRoot, 'tests/fixtures/missing.fzz'), {}), /cannot read/)
})

test('renders the schematic view of a .fz', { skip: !existsSync(bin) && 'build first' }, async () => {
  const roots = ['parts', 'fzpz'].map(r => join(repoRoot, 'tests/fixtures', r)).join(':')
  const saved = process.env.FRITZING_PARTS
  process.env.FRITZING_PARTS = roots
  try {
    const out = await renderSketchFile(bin, join(repoRoot, 'tests/fixtures/schematic.fz'), { ppi: 90, view: 'schematic' })
    assert.match(out.svg, />SDA</)
    assert.match(out.svg, /<circle/)
  } finally {
    if (saved === undefined) delete process.env.FRITZING_PARTS
    else process.env.FRITZING_PARTS = saved
  }
})
