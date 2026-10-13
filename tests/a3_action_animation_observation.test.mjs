import assert from 'node:assert/strict';
import test from 'node:test';
import {comparePairedActionAnimationFrames, projectActionObservationPlayer} from '../scripts/a3_local_room_full_route.mjs';

const player = (source_port, animation_frame) => ({source_port, stocks: 4,
  motion: 42, groundAir: 0, damage_percent: 0, x: 12.5, y: 0, animation_frame});

test('action player projection retains the producer-owned animation frame', () => {
  assert.deepEqual(projectActionObservationPlayer(player(1, 23.5)), {
    source_port: 1, stocks: 4, motion: 42, groundAir: 0,
    damage_percent: 0, x: 12.5, y: 0, animation_frame: 23.5,
  });
  assert.throws(() => projectActionObservationPlayer(player(1, undefined)),
    /lacks a finite native animation_frame/);
  assert.throws(() => projectActionObservationPlayer(player(1, Number.NaN)),
    /lacks a finite native animation_frame/);
});

test('paired same-cursor frames are compared and disagreement remains diagnostic', () => {
  const alpha = {players: [player(0, 3.25), player(1, 19.5)]};
  const beta = {players: [player(0, 3.25), player(1, 20.5)]};
  assert.deepEqual(comparePairedActionAnimationFrames([alpha, beta]), [
    {source_port: 0, alpha_animation_frame: 3.25, beta_animation_frame: 3.25,
      equal_at_common_source_cursor: true},
    {source_port: 1, alpha_animation_frame: 19.5, beta_animation_frame: 20.5,
      equal_at_common_source_cursor: false},
  ]);
  assert.throws(() => comparePairedActionAnimationFrames([alpha, {players: [player(1, 3), player(0, 4)]}]),
    /changed source player slots/);
  assert.throws(() => comparePairedActionAnimationFrames([alpha,
    {players: [player(0, 3.25), player(1, Infinity)]}]),
  /lacks a finite native animation_frame/);
});
