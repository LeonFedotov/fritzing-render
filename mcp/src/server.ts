#!/usr/bin/env -S npx tsx
// MCP server for fritzing-render: find Fritzing parts, read their
// connectors, and render breadboard diagrams as images the model can see.

import { writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'

import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js'
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js'
import { z } from 'zod'

import { binaryPath, describePart, formatPart, formatSearch, renderDiagram, searchParts } from './cli.ts'

const bin = binaryPath(process.env, resolve(import.meta.dirname, '../..'))

const server = new McpServer({ name: 'fritzing-render', version: '0.1.0' })

function failure(error: unknown): { isError: true, content: { type: 'text', text: string }[] } {
  return { isError: true, content: [{ type: 'text', text: error instanceof Error ? error.message : String(error) }] }
}

server.registerTool(
  'search_parts',
  {
    title: 'Search Fritzing parts',
    description:
      'Search the Fritzing parts libraries (Fritzing core and contrib, Adafruit) by words in the title, tags, family or file name. ' +
      'Every word must match. Returns each part\'s title and the "part" reference to use in render_diagram.',
    inputSchema: {
      query: z.string().min(1).describe('e.g. "arduino nano", "dht22", "2.2 tft", "pushbutton"'),
      limit: z.number().int().min(1).max(50).default(10),
    },
  },
  async ({ query, limit }) => {
    try {
      return { content: [{ type: 'text', text: formatSearch(await searchParts(bin, query, limit)) }] }
    } catch (error) {
      return failure(error)
    }
  },
)

server.registerTool(
  'describe_part',
  {
    title: 'Describe a Fritzing part',
    description:
      'A part\'s breadboard size and its connectors: name, id and part-local position (90 units per inch). ' +
      'Use it to choose connector names for wires and to plan where parts go.',
    inputSchema: {
      part: z.string().min(1).describe('A "part" reference from search_parts, a moduleId, or an exact title'),
    },
  },
  async ({ part }) => {
    try {
      return { content: [{ type: 'text', text: formatPart(await describePart(bin, part)) }] }
    } catch (error) {
      return failure(error)
    }
  },
)

const point = z.tuple([z.number(), z.number()])

server.registerTool(
  'render_diagram',
  {
    title: 'Render a breadboard diagram',
    description:
      'Render Fritzing breadboard-view parts and wires to an image (returned as PNG), optionally saving the SVG and PNG. ' +
      'Coordinates are Fritzing scene units, 90 per inch (header pins are 9 apart), y down; x/y place a part\'s top-left corner ' +
      'before rotation, and rotate (0/90/180/270, clockwise) turns it about its centre. ' +
      'Wires join "<part id>.<connector name or id>" ends, optionally through "via" points, so they can be routed at right angles. ' +
      'Colors: Fritzing wire colors (blue, red, black, yellow, green, grey, white, orange, ochre, cyan, brown, purple, pink) or #rrggbb.',
    inputSchema: {
      parts: z
        .array(z.object({
          id: z.string().min(1),
          part: z.string().min(1).optional().describe('reference from search_parts'),
          generic: z
            .object({ title: z.string(), pins: z.array(z.string()).min(1) })
            .optional()
            .describe('instead of "part", for parts the libraries lack: a labelled block with header pins along its bottom'),
          x: z.number().default(0),
          y: z.number().default(0),
          rotate: z.union([z.literal(0), z.literal(90), z.literal(180), z.literal(270)]).default(0),
          label: z.string().optional().describe('text drawn above the part'),
          labelBelow: z.boolean().optional().describe('draw the label under the part instead'),
          color: z.string().optional().describe('recolors an LED-style part: a Fritzing LED color like "Green (555nm)", a word like green, or #rrggbb'),
        }).refine(p => Boolean(p.part) !== Boolean(p.generic), { message: 'give each part either "part" or "generic"' }))
        .min(1),
      wires: z
        .array(z.object({
          from: z.string().describe('e.g. "mcu.D2"'),
          to: z.string(),
          color: z.string().default('blue'),
          via: z.array(point).optional().describe('bend points, scene units'),
        }))
        .default([]),
      margin: z.number().min(0).default(18),
      ppi: z.number().min(30).max(600).default(150).describe('PNG resolution'),
      transparent: z.boolean().default(false),
      save_svg: z.string().optional().describe('absolute path to also write the SVG to'),
      save_png: z.string().optional().describe('absolute path to also write the PNG to'),
    },
  },
  async ({ parts, wires, margin, ppi, transparent, save_svg, save_png }) => {
    try {
      const out = await renderDiagram(bin, { parts, wires, margin }, { ppi, transparent })
      const saved: string[] = []
      if (save_svg) {
        await writeFile(save_svg, out.svg)
        saved.push(save_svg)
      }
      if (save_png) {
        await writeFile(save_png, out.png)
        saved.push(save_png)
      }
      const summary = `Rendered ${parts.length} parts and ${wires.length} wires.` + (saved.length ? ` Saved: ${saved.join(', ')}` : '')
      return {
        content: [
          { type: 'image', data: out.png.toString('base64'), mimeType: 'image/png' },
          { type: 'text', text: summary },
        ],
      }
    } catch (error) {
      return failure(error)
    }
  },
)

await server.connect(new StdioServerTransport())
