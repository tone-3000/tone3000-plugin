/**
 * The pure rule behind the gallery tile's model arrows: where the active model
 * sits in a tone's list, and which models are on either side of it.
 *
 * Plain `node --test`, like the rest of this directory:
 *
 *   node --test ui/test/
 */
import test from 'node:test';
import assert from 'node:assert/strict';

import { modelStep } from '../src/types/tone.ts';

// Ids are deliberately neither sequential nor in order, so a step that
// confuses a model's id with its position in the list cannot pass.
const models = [{ id: 501 }, { id: 17 }, { id: 902 }];

test('a middle model has a neighbour on each side, in list order', () => {
  const step = modelStep(models, 17);
  assert.equal(step?.index, 1);
  assert.equal(step?.count, 3);
  assert.equal(step?.prev?.id, 501);
  assert.equal(step?.next?.id, 902);
});

test('the first model has nothing before it and the last nothing after', () => {
  const first = modelStep(models, 501);
  assert.equal(first?.index, 0);
  assert.equal(first?.prev, undefined);
  assert.equal(first?.next?.id, 17);

  const last = modelStep(models, 902);
  assert.equal(last?.index, 2);
  assert.equal(last?.prev?.id, 17);
  assert.equal(last?.next, undefined);
});

test('a tone with one model or none has nothing to step through', () => {
  assert.equal(modelStep([{ id: 501 }], 501), null);
  assert.equal(modelStep([], 501), null);
});

test('an active model missing from the list steps nowhere, rather than from the start', () => {
  // A stored model the fetched catalog does not contain: findIndex gives -1,
  // and stepping from -1 would wrongly offer the first model as "next".
  assert.equal(modelStep(models, 12345), null);
});
