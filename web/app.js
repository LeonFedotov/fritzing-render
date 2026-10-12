// The sketches page: the examples, each in either view, and a sketch you drop.

import { addUserParts, card, dropZone, fetchBytes, isPartFile, whenSeen } from './lib.js'

const FRITZING_APP = 'https://raw.githubusercontent.com/fritzing/fritzing-app/5aa56a510183c23084990a6b4481708cad24c15b/sketches/core/'

const EXAMPLES = [
  { title: 'Blink', file: 'examples/blink.fzz', about: 'An Arduino Uno blinking an LED through a 220 Ω resistor: a small hand-written .fzz.' },
  { title: 'AnalogInputPot', file: FRITZING_APP + 'AnalogInputPot.fzz', about: "Fritzing's own example: a potentiometer read on A0, an LED on D13." },
  { title: 'PingPong', file: FRITZING_APP + 'Fritzing%20Creator%20Kit%20DE%2BEN/creator-kit-en/Fritzing/PingPong.fzz', theme: 'modern', view: 'schematic', about: 'From the Fritzing Creator Kit: an 8×8 LED matrix game with net labels and power symbols.' },
  { title: 'Button', file: FRITZING_APP + 'Button.fzz', about: "Fritzing's own example: a pushbutton on D2." },
  { title: 'ODROID-SHOW2 station', file: 'examples/odroid-show2.json', about: 'An ODROID-SHOW2 on a LiPo, with a RedBear BLE Nano as its Bluetooth relay (parts from fritzing-parts-extra).' },
  { title: 'FeatherS2 air station', file: 'examples/feathers2-air.json', about: 'A FeatherS2 with PMSA003I, SGP30 and BME680 breakouts and a BLE Nano bridge.' },
  { title: 'ESP32 air station', file: 'examples/esp32-air-station.json', view: 'schematic', theme: 'modern', about: 'An ESP32 DevKit with an MH-Z19, AM2302, PMSA003I and a microSD card.' },
  { title: 'BLE Nano on a breadboard', file: 'examples/ble-nano.json', about: 'The RedBear BLE Nano v1.5 part, drawn from its gerbers, straddling the gap.' },
]

const gallery = document.querySelector('#gallery')
for (const ex of EXAMPLES) {
  const name = ex.file.split('/').pop().replace(/\.(fzz|json)$/, '')
  let cached
  const edit = ex.file.endsWith('.json') ? `editor.html?open=${encodeURIComponent(ex.file)}` : undefined
  const c = card({ ...ex, name, edit, bytes: async () => (cached ??= await fetchBytes(ex.file)) }, gallery)
  whenSeen(c.el, c.draw)
}

// A dropped sketch (.fzz, .fz, .json) is drawn, after any parts dropped with it.
const yours = document.querySelector('#yours-out')
const note = document.querySelector('#drop-note')
dropZone(document.querySelector('#drop'), async files => {
  const partFiles = files.filter(isPartFile)
  const sketch = files.find(f => /\.(fzz|fz|json)$/i.test(f.name))
  note.textContent = ''
  if (partFiles.length) {
    try {
      const added = await addUserParts(partFiles)
      if (added.length) note.innerHTML = `Added ${added.map(p => p.title.replace(/[<&]/g, '')).join(', ')} to <a href="parts.html#yours">your parts</a>.`
    } catch (e) {
      note.textContent = String(e.message ?? e)
    }
  }
  if (sketch) {
    const bytes = new Uint8Array(await sketch.arrayBuffer())
    yours.replaceChildren()
    card({ title: sketch.name, about: '', bytes: async () => bytes, name: sketch.name.replace(/\.\w+$/, '') }, yours).draw()
  }
})
