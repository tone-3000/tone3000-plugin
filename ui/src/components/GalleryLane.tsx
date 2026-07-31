import React from 'react';
import { SortableContext, horizontalListSortingStrategy } from '@dnd-kit/sortable';
import { ArrowUpDown, Link, PlusCircle } from 'lucide-react';
import { GalleryBlock, AddTile } from './GalleryBlock';
import type { AddTileRouting } from './GalleryBlock';
import { KnobControl } from './KnobControl';
import { panScale } from './knobScale';

const PAN_LEFT_SCALE = panScale('left');
const PAN_RIGHT_SCALE = panScale('right');
import { ChromeIconButton } from './ChromeIconButton';
import { HELP, helpProps } from './helpText';
import { BORDER, ICON_BOX_SIZE, ICON_SIZE, KNOB_SIZE_SECONDARY, WHITE } from './theme';
import { useParameter } from '../hooks/useParameter';
import { useChainActions } from '../hooks/useChainActions';
import type { ChainBranch, ChainItem, ChainSide } from '../types/chain';
import { isInsertSlot } from '../types/chain';
/**
 * Lane-level pieces of the chain gallery (see ChainView for the drag
 * orchestration that owns them): the ghost rail, a single lane of tiles,
 * the scroll-edge fades and the stereo pan rail.
 */

export const TILE_SIZE = 224;
/** Stereo shows two lanes, so its tiles shrink to fit the fixed height. */
export const STEREO_TILE_SIZE = 160;
/** Gap between tiles — the visible run of each connector line. */
export const TILE_GAP = 24;
/** Vertical gap between the two stereo lanes. */
export const LANE_GAP = 24;
/** Radius of the ghost rail's PlusCircle glyphs (size 40). */
const RAIL_CIRCLE_RADIUS = 20;
/** Gutter inside the scroll area — tiles fade out under it while scrolling. */
export const EDGE_FADE_WIDTH = 32;

/** Signal-flow routing lines for an add tile at the given lane position. */
const addTileRouting = (index: number, count: number): AddTileRouting => {
  if (count <= 1) return 'none';
  if (index === 0) return 'right';
  if (index === count - 1) return 'left';
  return 'both';
};

// ── Chain branching (stereo mode) ──
// A branch taps one lane's signal on a connector gap and feeds it to the
// other lane. The affordances live on the gaps between tiles and stay
// invisible until the gap is hovered (CSS :hover — see index.css) so the
// resting state is just the connector lines. Hovering a gap reveals a
// filled white dot that sets — or re-points, when a branch already exists —
// the branch after the tile to its left; hovering the active tap gap
// reveals the same dot, which clears the branch on click. The two-lane
// elbow connector is drawn by ChainView (it spans both lanes).

/** Diameter of the branch dots — half the power-button chrome footprint. */
export const BRANCH_CIRCLE_SIZE = ICON_BOX_SIZE / 2;

/** X center of the connector gap *before* the tile at `index` (i.e. gap g
    sits between tiles g-1 and g), in lane-content coordinates. */
export const gapCenterX = (gapIndex: number, tileSize: number) =>
  gapIndex * (tileSize + TILE_GAP) - TILE_GAP / 2;

/** Filled white disc — set-branch and clear-branch share the same look. */
const branchDotStyle: React.CSSProperties = {
  width: `${BRANCH_CIRCLE_SIZE}px`,
  height: `${BRANCH_CIRCLE_SIZE}px`,
  borderRadius: '50%',
  backgroundColor: WHITE,
  border: 'none',
  cursor: 'pointer',
  padding: 0,
  boxSizing: 'border-box',
  flexShrink: 0,
};

/** Full-gap hover zone wrapping a branch dot: the whole 24px connector
    run is the hit/hover area, the button itself stays hidden until then. */
const branchGapStyle = (centerX: number): React.CSSProperties => ({
  position: 'absolute',
  left: `${centerX - TILE_GAP / 2}px`,
  top: 0,
  bottom: 0,
  width: `${TILE_GAP}px`,
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  pointerEvents: 'auto',
});

/**
 * Interactive branch layer over a lane's connector gaps (stereo mode only).
 * Every gap following a tone block carries a hover-revealed filled dot that
 * sets (or, while branched, re-points — one move, no clearing first) the
 * branch to that spot. The one exception is the active tap gap on the trunk
 * lane, whose dot clears the branch instead.
 */
const BranchRail: React.FC<{
  items: ChainItem[];
  tileSize: number;
  side: ChainSide;
  branch: ChainBranch | null;
  /** False while a drag is in flight (gap hit targets would fight drops). */
  interactive: boolean;
  onSetBranch: (afterBlockId: string) => void;
  onClearBranch: () => void;
}> = ({ items, tileSize, side, branch, interactive, onSetBranch, onClearBranch }) => {
  const isTrunk = branch != null && branch.side === side;
  const tapIndex = isTrunk ? items.findIndex((i) => i.blockId === branch.afterBlockId) : -1;

  return (
    <div style={{ position: 'absolute', inset: 0, pointerEvents: 'none', zIndex: 3 }}>
      {interactive &&
        items.map((item, index) => {
          // The tap point is a tone block's output — the gap after it.
          if (index === items.length - 1 || isInsertSlot(item)) return null;
          // The active tap gap carries the clear button below instead.
          if (isTrunk && index === tapIndex) return null;
          return (
            <div
              key={`${item.blockId}-branch-gap`}
              className="branch-gap"
              style={branchGapStyle(gapCenterX(index + 1, tileSize))}
            >
              <button
                type="button"
                className="branch-gap-button"
                onClick={() => onSetBranch(item.blockId)}
                aria-label="Branch from here"
                {...helpProps(HELP.branchGap)}
                style={branchDotStyle}
              />
            </div>
          );
        })}
      {isTrunk && tapIndex !== -1 && (
        <div className="branch-gap" style={branchGapStyle(gapCenterX(tapIndex + 1, tileSize))}>
          <button
            type="button"
            className="branch-gap-button"
            onClick={onClearBranch}
            aria-label="Make chains independent"
            {...helpProps(HELP.branchJunction)}
            style={branchDotStyle}
          />
        </div>
      )}
    </div>
  );
};

/**
 * Static ghost rail behind a lane: one plus circle per slot, connector lines
 * between them. The circles sit hidden behind the (opaque) tiles and appear
 * when a slot is vacated mid-drag; only the line runs inside the gaps are
 * visible otherwise. Mirrors the old vertical chain's background exactly.
 */
const GhostRail: React.FC<{ slots: number; tileSize: number }> = ({ slots, tileSize }) => (
  <div
    style={{
      position: 'absolute',
      inset: 0,
      display: 'flex',
      flexDirection: 'row',
      alignItems: 'center',
      gap: `${TILE_GAP}px`,
      pointerEvents: 'none',
      zIndex: 1,
    }}
  >
    {Array.from({ length: slots }, (_, i) => (
      <span
        key={`${i}-rail`}
        style={{
          width: `${tileSize}px`,
          height: `${tileSize}px`,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          position: 'relative',
          flexShrink: 0,
        }}
      >
        {i > 0 && (
          <div
            style={{
              position: 'absolute',
              left: `${-(TILE_GAP + tileSize / 2 - RAIL_CIRCLE_RADIUS)}px`,
              top: '50%',
              width: `${TILE_GAP + tileSize - 2 * RAIL_CIRCLE_RADIUS}px`,
              height: '1px',
              backgroundColor: '#ffffff',
            }}
          />
        )}
        <PlusCircle size={40} strokeWidth={1} />
      </span>
    ))}
  </div>
);

/** One lane of tiles over its ghost rail (no scroll of its own — both lanes
    share the outer scroll area). Native keeps every lane at its minimum slot
    layout (5 tiles, always ≥1 insert), so each item here is a real block —
    insert slots included — and every tile is reorderable. */
export const GalleryLane: React.FC<{
  items: ChainItem[];
  tileSize: number;
  /** Stereo lanes use the 3×3 grip; mono uses the horizontal grip. */
  stereo?: boolean;
  onOpen: (blockId: string) => void;
  /** Open the tone browser targeting the clicked insert slot. */
  onAdd: (insertBlockId: string) => void;
  /** Paste the copied block into the insert slot at this lane index; null
      while there's nothing valid to paste (insert action sheets show Paste
      disabled). */
  onPasteBlock?: ((index: number) => void) | null;
  /** Which lane this is — keys the branch affordances (stereo only). */
  side?: ChainSide;
  /** Active branch (stereo only) — drives the junction node on the trunk. */
  branch?: ChainBranch | null;
  /** Show the hover branch buttons on the connector gaps (stereo, no drag
      in flight). */
  branchInteractive?: boolean;
  onSetBranch?: (afterBlockId: string) => void;
  onClearBranch?: () => void;
}> = ({
  items,
  tileSize,
  stereo = false,
  onOpen,
  onAdd,
  onPasteBlock = null,
  side = 'left',
  branch = null,
  branchInteractive = false,
  onSetBranch,
  onClearBranch,
}) => (
  <div style={{ position: 'relative', width: 'max-content' }}>
    <GhostRail slots={items.length} tileSize={tileSize} />
    {stereo && (branchInteractive || branch != null) && (
      <BranchRail
        items={items}
        tileSize={tileSize}
        side={side}
        branch={branch}
        interactive={branchInteractive}
        onSetBranch={onSetBranch ?? (() => {})}
        onClearBranch={onClearBranch ?? (() => {})}
      />
    )}
    <SortableContext
      items={items.map((item) => item.blockId)}
      strategy={horizontalListSortingStrategy}
    >
      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'center',
          gap: `${TILE_GAP}px`,
          position: 'relative',
          zIndex: 2,
        }}
      >
        {items.map((item, index) =>
          isInsertSlot(item) ? (
            <AddTile
              key={item.blockId}
              id={item.blockId}
              size={tileSize}
              stereo={stereo}
              routing={addTileRouting(index, items.length)}
              onClick={() => onAdd(item.blockId)}
              onPaste={onPasteBlock != null ? () => onPasteBlock(index) : null}
            />
          ) : (
            <GalleryBlock
              key={item.blockId}
              block={item}
              size={tileSize}
              stereo={stereo}
              onOpen={onOpen}
            />
          )
        )}
      </div>
    </SortableContext>
  </div>
);

/**
 * Left rail for stereo: per-lane pan knobs (each centered on its lane), with
 * the link toggle and whole-chain swap on the seam between them. Constant-
 * power pan positions (0 = hard left, 1 = hard right): Pan Left covers hard
 * left..center on a half track, Pan Right center..hard right. Linked (default)
 * mirrors the knobs so width changes stay symmetric.
 */
export const StereoPanRail: React.FC = () => {
  const { swapChains } = useChainActions();
  const [panLeft, setPanLeft] = useParameter('chainPanLeft', 'slider');
  const [panRight, setPanRight] = useParameter('chainPanRight', 'slider');
  const [linked, setLinked] = useParameter('chainPanLinked', 'toggle');

  const handlePanLeft = (value: number) => {
    setPanLeft(value);
    if (linked) setPanRight(1 - value);
  };
  const handlePanRight = (value: number) => {
    setPanRight(value);
    if (linked) setPanLeft(1 - value);
  };
  const handleToggleLink = () => {
    const next = !linked;
    setLinked(next);
    // Re-linking snaps back to a symmetric image, anchored on the left pan.
    if (next) setPanRight(1 - panLeft);
  };

  // Each knob region: knob centered on its lane, with a hairline connector
  // filling the remaining run between the knob and the link/swap box so the
  // pan controls read as one wired-together group.
  const knobRegion: React.CSSProperties = {
    flex: 1,
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
  };
  const spacer: React.CSSProperties = { flex: 1 };
  const connector: React.CSSProperties = {
    flex: 1,
    width: 0,
    borderLeft: BORDER,
    margin: '6px 0',
  };

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        alignSelf: 'center',
        height: `${STEREO_TILE_SIZE * 2 + LANE_GAP}px`,
        flexShrink: 0,
      }}
    >
      <div style={knobRegion}>
        <div style={spacer} />
        <KnobControl
          label="Pan Left"
          value={panLeft}
          onChange={handlePanLeft}
          variant="panLeft"
          min={0}
          max={0.5}
          size={KNOB_SIZE_SECONDARY}
          labelSize={10}
          thumb="secondary"
          scale={PAN_LEFT_SCALE}
          defaultValue={0}
          help={HELP.panLeft}
        />
        <div style={connector} />
      </div>
      <div
        style={{
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'center',
          gap: '4px',
          border: BORDER,
          borderRadius: '9999px',
          padding: '3px 5px',
        }}
      >
        <ChromeIconButton
          tone="link"
          on={linked}
          help={HELP.panLink}
          onClick={handleToggleLink}
          offsetY={0}
        >
          <Link size={ICON_SIZE} />
        </ChromeIconButton>
        <ChromeIconButton help={HELP.swapChains} onClick={swapChains} offsetY={0}>
          <ArrowUpDown size={ICON_SIZE} />
        </ChromeIconButton>
      </div>
      <div style={knobRegion}>
        <div style={connector} />
        <KnobControl
          label="Pan Right"
          value={panRight}
          onChange={handlePanRight}
          variant="panRight"
          min={0.5}
          max={1}
          size={KNOB_SIZE_SECONDARY}
          labelSize={10}
          thumb="secondary"
          scale={PAN_RIGHT_SCALE}
          defaultValue={1}
          help={HELP.panRight}
        />
        <div style={spacer} />
      </div>
    </div>
  );
};

/**
 * The two-lane elbow of an active branch: a vertical drop from the trunk
 * lane's tap gap to the branch lane's row, plus the short horizontal stub
 * into the branch lane's first tile — same 1px hairlines as the ghost rail.
 * Positioned by ChainView inside the lanes column (it spans both lanes);
 * `x` is the tap gap's center in column coordinates.
 */
export const BranchElbow: React.FC<{ x: number; tileSize: number; trunkOnTop: boolean }> = ({
  x,
  tileSize,
  trunkOnTop,
}) => {
  const topLaneCenter = tileSize / 2;
  const bottomLaneCenter = tileSize + LANE_GAP + tileSize / 2;
  const stubY = trunkOnTop ? bottomLaneCenter : topLaneCenter;
  const line: React.CSSProperties = {
    position: 'absolute',
    backgroundColor: '#ffffff',
    pointerEvents: 'none',
    zIndex: 1,
  };
  return (
    <>
      <div
        style={{
          ...line,
          left: `${x}px`,
          top: `${topLaneCenter}px`,
          width: '1px',
          height: `${bottomLaneCenter - topLaneCenter}px`,
        }}
      />
      <div
        style={{
          ...line,
          left: `${x}px`,
          top: `${stubY}px`,
          width: `${TILE_GAP / 2}px`,
          height: '1px',
        }}
      />
    </>
  );
};

/** Fade the lanes out under the gutters as they scroll — content slides
    behind a smooth ramp to the background instead of hard-clipping. */
export const EdgeFade: React.FC<{ side: 'left' | 'right' }> = ({ side }) => (
  <div
    style={{
      position: 'absolute',
      top: 0,
      bottom: 0,
      [side]: 0,
      width: `${EDGE_FADE_WIDTH}px`,
      background: `linear-gradient(to ${side === 'left' ? 'right' : 'left'}, #000000, rgba(0, 0, 0, 0))`,
      pointerEvents: 'none',
      zIndex: 3,
    }}
  />
);
