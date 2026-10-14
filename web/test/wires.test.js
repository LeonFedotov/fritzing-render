import { test } from 'node:test'
import assert from 'node:assert/strict'
import { insertJoint, moveJoint, nearestSegment, removeJoint } from '../wires.js'

// a wire from (0,0) to (100,0), bent at (50,50)
const path = [[0, 0], [50, 50], [100, 0]]

test('nearestSegment finds the segment under a point and the point on it', () => {
  assert.deepEqual(nearestSegment(path, [26, 24]), { index: 0, at: [25, 25], distance: Math.hypot(1, 1) })
  assert.equal(nearestSegment(path, [80, 25]).index, 1)
})

test('nearestSegment keeps to the segment, past its ends', () => {
  assert.deepEqual(nearestSegment([[0, 0], [10, 0]], [-5, 0]).at, [0, 0])
  assert.deepEqual(nearestSegment([[0, 0], [10, 0]], [15, 3]).at, [10, 0])
})

test('nearestSegment of a zero-length segment is its point', () => {
  assert.deepEqual(nearestSegment([[4, 4], [4, 4]], [7, 8]), { index: 0, at: [4, 4], distance: 5 })
})

test('insertJoint puts the joint between the ends of the segment it falls on', () => {
  assert.deepEqual(insertJoint([[50, 50]], path, [25, 25]), { via: [[25, 25], [50, 50]], joint: 0 })
  assert.deepEqual(insertJoint([[50, 50]], path, [75, 25]), { via: [[50, 50], [75, 25]], joint: 1 })
})

test('insertJoint on a straight wire makes its first joint', () => {
  assert.deepEqual(insertJoint(undefined, [[0, 0], [90, 0]], [45, 2]), { via: [[45, 2]], joint: 0 })
})

test('moveJoint and removeJoint leave the other joints, and the input, alone', () => {
  const via = [[10, 10], [20, 20], [30, 30]]
  assert.deepEqual(moveJoint(via, 1, [25, 15]), [[10, 10], [25, 15], [30, 30]])
  assert.deepEqual(removeJoint(via, 0), [[20, 20], [30, 30]])
  assert.deepEqual(via, [[10, 10], [20, 20], [30, 30]])
})

