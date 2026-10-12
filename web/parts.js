// The parts catalogue: search every library, and your own parts. Only the
// results on screen are fetched and drawn; each links to its own page.

import { USER, addUserParts, clearQueue, dropZone, isPartFile, partCard, ready, removeUserPart, search, userEntry } from './lib.js'

const PAGE = 24
const results = document.querySelector('#results')
const more = document.querySelector('#more')
const count = document.querySelector('#count')
const q = document.querySelector('#q')
let hits = []
let shown = 0

function showMore() {
  for (const p of hits.slice(shown, shown + PAGE)) partCard(p, results)
  shown = Math.min(hits.length, shown + PAGE)
  more.hidden = shown >= hits.length
}

async function find(query) {
  await ready
  clearQueue()
  hits = search(query)
  shown = 0
  results.replaceChildren()
  count.textContent = query.trim() ? `${hits.length.toLocaleString()} part${hits.length === 1 ? '' : 's'}` : ''
  showMore()
}

// The query lives in the address (?q=), so a search can be linked and Back returns to it.
let timer
q.addEventListener('input', () => {
  clearTimeout(timer)
  timer = setTimeout(() => {
    const u = new URL(location.href)
    if (q.value.trim()) u.searchParams.set('q', q.value)
    else u.searchParams.delete('q')
    history.replaceState(null, '', u)
    find(q.value)
  }, 200)
})
more.onclick = showMore
const initial = new URLSearchParams(location.search).get('q')
if (initial) {
  q.value = initial
  find(initial)
}

// --- your parts ---------------------------------------------------------------------

const userList = document.querySelector('#user-parts')
function showUserParts(fresh = []) {
  userList.replaceChildren()
  document.querySelector('#user-parts-empty').hidden = USER.size > 0
  for (const u of USER.values()) {
    const c = partCard(userEntry(u), userList, {
      missing: u.missing,
      onRemove: async () => {
        await removeUserPart(u.key)
        showUserParts()
      },
    })
    if (fresh.includes(u.key)) c.scrollIntoView({ block: 'nearest' })
  }
}
ready.then(() => showUserParts())

const note = document.querySelector('#drop-note')
dropZone(document.querySelector('#drop'), async files => {
  note.textContent = ''
  try {
    const added = await addUserParts(files.filter(isPartFile))
    note.textContent = added.length ? `Added ${added.map(p => p.title).join(', ')}.` : 'No parts in those files.'
    showUserParts(added.map(p => p.key))
  } catch (e) {
    note.textContent = String(e.message ?? e)
  }
})
