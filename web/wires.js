// A wire's joints: the bend points ("via") between its two ends, in scene
// units. Joints stay where they are put; when a part moves, only the
// segments from its pins to the nearest joints follow it. Points are [x, y];
// nothing here changes its input.

// The segment of a path (its ends and joints, in order) nearest to `p`:
// its index (from path[index] to path[index + 1]), the nearest point on it
// and how far that is.
export function nearestSegment(path, p) {
  let best = null
  for (let i = 0; i + 1 < path.length; i++) {
    const [a, b] = [path[i], path[i + 1]]
    const [dx, dy] = [b[0] - a[0], b[1] - a[1]]
    const len2 = dx * dx + dy * dy
    const t = len2 ? Math.min(1, Math.max(0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / len2)) : 0
    const at = [a[0] + t * dx, a[1] + t * dy]
    const distance = Math.hypot(p[0] - at[0], p[1] - at[1])
    if (!best || distance < best.distance) best = { index: i, at, distance }
  }
  return best
}

// A new joint at `p`, on the segment of `path` it falls nearest: the wire's
// joints with it, and its index among them.
export function insertJoint(via = [], path, p) {
  const { index } = nearestSegment(path, p)
  return { via: [...via.slice(0, index), p, ...via.slice(index)], joint: index }
}

export const moveJoint = (via, j, p) => via.map((q, i) => i === j ? p : q)

export const removeJoint = (via, j) => via.filter((_, i) => i !== j)

