// Thin wrapper around the fritzing-render CLI: locating it, building its
// arguments, running it, and turning its JSON into text for the model.

import { execFile } from 'node:child_process'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

export type PartHit = {
  title: string
  ref: string
  moduleId: string
  family: string
  path: string
  tags: string[]
}

export type ConnectorInfo = {
  id: string
  name: string
  description: string
  x: number
  y: number
  found: boolean
}

export type PartInfo = {
  title: string
  ref: string
  moduleId: string
  path: string
  svg: string
  width: number
  height: number
  connectors: ConnectorInfo[]
}

export type Sketch = {
  parts: {
    id: string
    part?: string
    generic?: { title: string, pins: string[] }
    x?: number
    y?: number
    rotate?: number
    label?: string
    labelBelow?: boolean
    color?: string
  }[]
  wires?: { from: string, to: string, color?: string, via?: [number, number][] }[]
  margin?: number
}

export type RenderOptions = { ppi?: number, transparent?: boolean }

export type Rendered = { png: Buffer, svg: string, warnings: string[] }

export function binaryPath(env: Record<string, string | undefined>, repoRoot: string): string {
  return env.FRITZING_RENDER_BIN ?? join(repoRoot, 'build', 'fritzing-render')
}

export function renderArgs(o: { sketchFile: string, svgFile: string, pngFile: string, ppi: number, transparent: boolean }): string[] {
  const args = ['render', o.sketchFile, '-o', o.svgFile, '--png', o.pngFile, '--ppi', String(o.ppi)]
  return o.transparent ? [...args, '--transparent'] : args
}

export function formatSearch(hits: PartHit[]): string {
  if (hits.length === 0) return 'No parts matched.'
  return hits
    .map(h => `${h.title}${h.family ? ` [${h.family}]` : ''}\n  part: ${JSON.stringify(h.ref)}`)
    .join('\n')
}

export function formatPart(p: PartInfo): string {
  const rows = p.connectors.map(c => {
    const at = c.found ? `(${round(c.x)}, ${round(c.y)})` : 'no breadboard geometry'
    return `  ${c.name.padEnd(14)} ${c.id.padEnd(12)} ${at.padEnd(16)} ${c.description}`.trimEnd()
  })
  return [
    p.title,
    `part: ${JSON.stringify(p.ref)}`,
    `breadboard size: ${round(p.width)} x ${round(p.height)} (90 units per inch; 0.1 in = 9)`,
    'connectors (use <part id>.<name or id> in wires; positions are part-local):',
    ...rows,
  ].join('\n')
}

function round(n: number): number {
  return Math.round(n * 10) / 10
}

// The CLI's "warning: ..." lines on stderr, without the prefix.
export function parseWarnings(stderr: string): string[] {
  return stderr
    .split('\n')
    .filter(line => line.startsWith('warning: '))
    .map(line => line.slice('warning: '.length).trim())
}

function run(bin: string, args: string[]): Promise<{ stdout: string, stderr: string }> {
  return new Promise((resolvePromise, reject) => {
    execFile(bin, args, { maxBuffer: 64 * 1024 * 1024 }, (error, stdout, stderr) => {
      if (error) {
        reject(new Error(stderr.trim() || error.message))
        return
      }
      resolvePromise({ stdout, stderr })
    })
  })
}

export async function searchParts(bin: string, query: string, limit: number): Promise<PartHit[]> {
  const words = query.split(/\s+/).filter(Boolean)
  return JSON.parse((await run(bin, ['search', ...words, '--limit', String(limit), '--json'])).stdout) as PartHit[]
}

export async function describePart(bin: string, ref: string): Promise<PartInfo> {
  return JSON.parse((await run(bin, ['part', ref, '--json'])).stdout) as PartInfo
}

async function renderFile(bin: string, dir: string, sketchFile: string, options: RenderOptions): Promise<Rendered> {
  const svgFile = join(dir, 'out.svg')
  const pngFile = join(dir, 'out.png')
  const { stderr } = await run(bin, renderArgs({ sketchFile, svgFile, pngFile, ppi: options.ppi ?? 150, transparent: options.transparent ?? false }))
  return { png: await readFile(pngFile), svg: await readFile(svgFile, 'utf8'), warnings: parseWarnings(stderr) }
}

async function inTempDir<T>(work: (dir: string) => Promise<T>): Promise<T> {
  const dir = await mkdtemp(join(tmpdir(), 'fritzing-render-'))
  try {
    return await work(dir)
  } finally {
    await rm(dir, { recursive: true, force: true })
  }
}

export function renderDiagram(bin: string, sketch: Sketch, options: RenderOptions): Promise<Rendered> {
  return inTempDir(async dir => {
    const sketchFile = join(dir, 'sketch.json')
    await writeFile(sketchFile, JSON.stringify(sketch))
    return renderFile(bin, dir, sketchFile, options)
  })
}

// A sketch file as it is: a Fritzing .fzz or .fz, or a JSON sketch.
export function renderSketchFile(bin: string, sketchFile: string, options: RenderOptions): Promise<Rendered> {
  return inTempDir(dir => renderFile(bin, dir, sketchFile, options))
}

export function renderSummary(what: string, rendered: Rendered, saved: string[]): string {
  return [
    `Rendered ${what}.` + (saved.length ? ` Saved: ${saved.join(', ')}` : ''),
    ...rendered.warnings.map(w => `warning: ${w}`),
  ].join('\n')
}
