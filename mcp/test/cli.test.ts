import { test } from 'node:test'
import assert from 'node:assert/strict'
import { existsSync } from 'node:fs'
import { join, resolve } from 'node:path'

import {
  binaryPath,
  describePart,
  formatPart,
  formatSearch,
  renderArgs,
  renderDiagram,
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

test('render errors come back as messages, not crashes', { skip: !haveBinary && 'build first' }, async () => {
  await assert.rejects(
    renderDiagram(bin, { parts: [{ id: 'mcu', part: 'core/Arduino Nano3(fix).fzp' }], wires: [{ from: 'mcu.D99', to: 'mcu.D2' }] }, {}),
    /no connector "D99"/,
  )
})
