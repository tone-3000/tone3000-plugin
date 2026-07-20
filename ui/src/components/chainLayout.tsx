/**
 * Shared detail-card dimensions (the takeover view's tone card and its EQ
 * editor). Lives in its own module (no component imports) to keep the
 * dependency graph acyclic.
 */

export const CARD_WIDTH = 800;
// Matches the main I/O meters (DbMeter height=368) so the detail view lines up
// with them: header (40) + body top pad (16) + 224px image + model select (36),
// with the leftover space distributed under the image (center column is
// space-between).
export const CARD_HEIGHT = 368;
