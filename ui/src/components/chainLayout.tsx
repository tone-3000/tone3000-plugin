/**
 * Shared detail-card dimensions (the takeover view's tone card and its EQ
 * editor). Lives in its own module (no component imports) to keep the
 * dependency graph acyclic.
 */

export const CARD_WIDTH = 800;
// Header (40) + body padding (24) + 200px image + gap (8) + model select (36).
export const CARD_HEIGHT = 308;
