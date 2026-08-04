/**
 * Shared detail-card dimensions (the takeover view's tone card and its EQ
 * editor). Lives in its own module (no component imports) to keep the
 * dependency graph acyclic.
 *
 * Sizes follow the Figma detail mock: 16px-radius card, ~45px chrome header,
 * 275px padded body. The ← BLOCK row sits above the card and is not included.
 */

export const CARD_WIDTH = 800;
/** Chrome row inside the bordered card (power + EQ/share/swap/trash). */
export const HEADER_HEIGHT = 45;
/** Body below the header hairline (padding inclusive). */
export const BODY_HEIGHT = 275;
/** Inset shared by the tone view and EQ views inside the card body. */
export const BODY_PADDING = 16;
/** Bordered card only; the ← BLOCK row sits above it and is not included. */
export const CARD_HEIGHT = HEADER_HEIGHT + BODY_HEIGHT;
/** Outer corner radius of the bordered detail card. */
export const CARD_RADIUS = 16;
