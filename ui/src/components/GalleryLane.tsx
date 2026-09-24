import React from 'react';
import { ArrowUpDown, Link, PlusCircle } from './icons';
import { GalleryBlock, AddTile, plusIconSize, plusCircleInset } from './GalleryBlock';
import type { AddTileRouting } from './GalleryBlock';
import { KnobControl } from './KnobControl';
import { panScale } from './knobScale';

const PAN_LEFT_SCALE = panScale('left');
const PAN_RIGHT_SCALE = panScale('right');
import { ChromeIconButton } from './ChromeIconButton';
import { HELP, helpProps } from './helpText';
import {
  BLACK,
  BORDER,
  BRAND_YELLOW,
  ICON_BOX_SIZE,
  ICON_SIZE,
  FONT_MONO,
  KNOB_SIZE_SECONDARY,
  MUTED,
  WHITE,
  segmentedCellStyle,
  segmentedGroupStyle,
  uiOffClass,
} from './theme';
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
/** Gap between tiles: the visible run of each connector line. */
export const TILE_GAP = 24;
/** Floor for fit-to-width shrinking (see fitTileSize): below this the
    artwork stops reading and the hover chrome (power, swap, trash) no
    longer fits a tile's top strip. */
export const MIN_TILE_SIZE = 128;
/** Vertical gap between the two stereo lanes. */
export const LANE_GAP = 24;
/** Gutter inside the scroll area; tiles fade out under it while scrolling. */
export const EDGE_FADE_WIDTH = 32;

/** Signal-flow routing lines for an add tile at the given lane position. */
const addTileRouting = (index: number, count: number): AddTileRouting => {
  if (count <= 1) return 'none';
  if (index === 0) return 'right';
  if (index === count - 1) return 'left';
  return 'both';
};

// Chain branching (stereo mode)
// A branch taps one lane's signal on a connector gap and feeds it to the
// other lane. The affordances live on the gaps between tiles and stay
// invisible until the gap is hovered (CSS :hover, see index.css) so the
// resting state is just the connector lines. Hovering a gap reveals a
// filled white dot that sets (or re-points, when a branch already exists)
// the branch after the tile to its left; hovering the active tap gap
// reveals the same dot, which clears the branch on click. The two-lane
// elbow connector is drawn by ChainView (it spans both lanes).

/** Diameter of the branch dots: half the power-button chrome footprint. */
export const BRANCH_CIRCLE_SIZE = ICON_BOX_SIZE / 2;

/** X center of the connector gap *before* the tile at `index` (i.e. gap g
    sits between tiles g-1 and g), in lane-content coordinates. */
/**
 * Tile edge that lets `slots` tiles sit side by side in a scroller
 * `scrollerWidth` design px wide (edge insets and gaps included), capped at
 * `baseSize` and floored at MIN_TILE_SIZE. At the full 224 px only three
 * tiles fit the plugin's width, so a 5-6 block rig lived behind a hidden
 * scrollbar and a wheel-only pan (issue #84); shrinking the row until it
 * fits keeps the whole chain in view, and the ordinary scroll takes over
 * only past the floor. 0 width (not measured yet) keeps the base size, so
 * the first paint never flashes a wrong size.
 */
export const fitTileSize = (baseSize: number, slots: number, scrollerWidth: number): number => {
  if (scrollerWidth <= 0 || slots <= 0) return baseSize;
  const usable = scrollerWidth - 2 * EDGE_FADE_WIDTH - (slots - 1) * TILE_GAP;
  return Math.max(MIN_TILE_SIZE, Math.min(baseSize, Math.floor(usable / slots)));
};

export const gapCenterX = (gapIndex: number, tileSize: number) =>
  gapIndex * (tileSize + TILE_GAP) - TILE_GAP / 2;

/** Filled white disc; set-branch and clear-branch share the same look. */
const branchDotStyle: React.CSSProperties = {
  width: `${BRANCH_CIRCLE_SIZE}rem`,
  height: `${BRANCH_CIRCLE_SIZE}rem`,
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
  left: `${centerX - TILE_GAP / 2}rem`,
  top: 0,
  bottom: 0,
  width: `${TILE_GAP}rem`,
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  pointerEvents: 'auto',
});

/**
 * Interactive branch layer over a lane's connector gaps (stereo mode only).
 * Every gap following a tone block carries a hover-revealed filled dot that
 * sets (or, while branched, re-points: one move, no clearing first) the
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
          // The tap point is a tone block's output, i.e. the gap after it.
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
      gap: `${TILE_GAP}rem`,
      pointerEvents: 'none',
      zIndex: 1,
    }}
  >
    {Array.from({ length: slots }, (_, i) => (
      <span
        key={`${i}-rail`}
        style={{
          width: `${tileSize}rem`,
          height: `${tileSize}rem`,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          position: 'relative',
          flexShrink: 0,
        }}
      >
        {i > 0 && (
          // Runs from the previous slot's plus ring to this slot's, extended
          // past each icon's bounding box by plusCircleInset so the line
          // actually meets the drawn circle (see GalleryBlock).
          <div
            style={{
              position: 'absolute',
              left: `${-(TILE_GAP + tileSize / 2 - plusIconSize(tileSize) / 2 + plusCircleInset(plusIconSize(tileSize)))}rem`,
              top: '50%',
              width: `${TILE_GAP + tileSize - plusIconSize(tileSize) + 2 * plusCircleInset(plusIconSize(tileSize))}rem`,
              height: '2rem',
              backgroundColor: '#ffffff',
              transform: 'translateY(-50%)',
            }}
          />
        )}
        <PlusCircle size={plusIconSize(tileSize)} strokeWidth={1} />
      </span>
    ))}
  </div>
);

/** One lane of tiles over its ghost rail (no scroll of its own; both lanes
    share the outer scroll area). Native keeps every lane at its minimum slot
    layout (5 tiles, always ≥1 insert), so each item here is a real block,
    insert slots included, and every tile is reorderable. While branched,
    native also trims the branch lane's surplus trailing inserts so its
    indented rail ends level with the trunk (see alignBranchLaneLengths). */
export const GalleryLane: React.FC<{
  items: ChainItem[];
  tileSize: number;
  /** Stereo mode: enables the branch affordances on the connector gaps. */
  stereo?: boolean;
  onOpen: (blockId: string) => void;
  /** Open the tone browser targeting the clicked insert slot. */
  onAdd: (insertBlockId: string) => void;
  /** Paste the copied block into the insert slot at this lane index; null
      while there's nothing valid to paste (insert action sheets show Paste
      disabled). */
  onPasteBlock?: ((index: number) => void) | null;
  /** Which lane this is; keys the branch affordances (stereo only). */
  side?: ChainSide;
  /** Active branch (stereo only); drives the junction node on the trunk. */
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
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'center',
        gap: `${TILE_GAP}rem`,
        position: 'relative',
        zIndex: 2,
      }}
    >
      {items.map((item, index) =>
        isInsertSlot(item) ? (
          <AddTile
            key={item.blockId}
            id={item.blockId}
            index={index}
            group={side}
            size={tileSize}
            routing={addTileRouting(index, items.length)}
            onClick={() => onAdd(item.blockId)}
            onPaste={onPasteBlock != null ? () => onPasteBlock(index) : null}
          />
        ) : (
          <GalleryBlock
            key={item.blockId}
            block={item}
            index={index}
            group={side}
            size={tileSize}
            onOpen={onOpen}
          />
        )
      )}
    </div>
  </div>
);

/** Per-lane solo + polarity as one segmented [S|Ø] under the pan label.
    Grey idle, house-armed yellow while engaged. Solo ("S") auditions its
    chain (exclusive: engaging one clears the other); polarity ("Ø") flips
    its chain's sign, for captures that land 180° out. Both act inside the
    native image matrix, on the same smoothers as pan moves, so neither
    clicks. */
const PanRailChips: React.FC<{
  solo: boolean;
  invert: boolean;
  soloHelp: string;
  invertHelp: string;
  onSolo: () => void;
  onInvert: () => void;
}> = ({ solo, invert, soloHelp, invertHelp, onSolo, onInvert }) => {
  const cell = (on: boolean): React.CSSProperties => ({
    ...segmentedCellStyle(false),
    minWidth: `${ICON_BOX_SIZE}rem`,
    color: on ? BLACK : MUTED,
    backgroundColor: on ? BRAND_YELLOW : 'transparent',
  });
  return (
    <div style={segmentedGroupStyle()}>
      <button type="button" onClick={onSolo} {...helpProps(soloHelp)} style={cell(solo)}>
        <span className="cap-trim">S</span>
      </button>
      <button type="button" onClick={onInvert} {...helpProps(invertHelp)} style={cell(invert)}>
        <span className="cap-trim">Ø</span>
      </button>
    </div>
  );
};

/**
 * Left rail for stereo: per-lane pan knobs (each centered on its lane) with
 * a solo/polarity [S|Ø] group under the label, and the link toggle and
 * whole-chain swap on the seam between them. Constant-power pan positions
 * (0 = hard left, 1 = hard right): Pan L covers hard left..center on a half
 * track, Pan R center..hard right. Linked (default) mirrors the knobs so
 * width changes stay symmetric.
 *
 * `monoSum` (a rig that can't reproduce stereo): native sums the chains to
 * mono, so the pans dim and go inert, and the seam pill swaps its link/swap
 * buttons for a MONO chip that says why. Solo and polarity stay live: they
 * act on the chains inside the sum.
 */
export const StereoPanRail: React.FC<{ monoSum: boolean; tileSize: number }> = ({
  monoSum,
  tileSize,
}) => {
  const { swapChains } = useChainActions();
  const [panLeft, setPanLeft, onPanLeftDrag] = useParameter('chainPanLeft', 'slider');
  const [panRight, setPanRight, onPanRightDrag] = useParameter('chainPanRight', 'slider');
  const [linked, setLinked] = useParameter('chainPanLinked', 'toggle');
  const [soloLeft, setSoloLeft] = useParameter('chainSoloLeft', 'toggle');
  const [soloRight, setSoloRight] = useParameter('chainSoloRight', 'toggle');
  const [invertLeft, setInvertLeft] = useParameter('chainInvertLeft', 'toggle');
  const [invertRight, setInvertRight] = useParameter('chainInvertRight', 'toggle');

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
  const toggleSoloLeft = () => {
    const next = !soloLeft;
    setSoloLeft(next);
    if (next) setSoloRight(false);
  };
  const toggleSoloRight = () => {
    const next = !soloRight;
    setSoloRight(next);
    if (next) setSoloLeft(false);
  };

  // Each knob region: knob + [S|Ø] centered on its lane, with a hairline
  // connector filling the remaining run between the knob and the link/swap
  // box so the pan controls read as one wired-together group.
  const knobRegion: React.CSSProperties = {
    flex: 1,
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
  };
  const spacer: React.CSSProperties = { flex: 1 };
  // 8rem gap on the knob/chips side; flush against the link/swap pill.
  const connector = (contentEdge: 'top' | 'bottom'): React.CSSProperties => ({
    flex: 1,
    width: 0,
    borderLeft: BORDER,
    margin: contentEdge === 'top' ? '10rem 0 0' : '0 0 10rem',
  });
  const panKnobWrap: React.CSSProperties = {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    gap: '8rem',
  };
  // Mono sum: the pans can't be heard, so they dim and go inert; the
  // wrapper carries the hint that says why. The [S|Ø] chips stay outside
  // the wrappers: solo and polarity act inside the sum.
  const panOffWrap: React.CSSProperties = { transition: 'opacity 0.2s ease' };
  const panOffHelp = monoSum ? helpProps(HELP.panMonoSum) : {};

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        alignSelf: 'center',
        // Tracks the lanes' actual (fit-to-width) tile size so the pans stay
        // level with their lanes.
        height: `${tileSize * 2 + LANE_GAP}rem`,
        flexShrink: 0,
        // Room for the left edge-fade's 1rem outer overhang (see EdgeFade)
        // so it doesn't sit on the link/swap pill.
        paddingRight: '1rem',
      }}
    >
      <div style={knobRegion}>
        <div style={spacer} />
        <div style={panKnobWrap}>
          <div className={uiOffClass(monoSum)} style={panOffWrap} {...panOffHelp}>
            <KnobControl
              label="Pan L"
              value={panLeft}
              onChange={handlePanLeft}
              variant="panLeft"
              min={0}
              max={0.5}
              size={KNOB_SIZE_SECONDARY}
              thumb="secondary"
              scale={PAN_LEFT_SCALE}
              defaultValue={0}
              help={HELP.panLeft}
              labelBright
              onDragStateChange={onPanLeftDrag}
            />
          </div>
          <PanRailChips
            solo={soloLeft}
            invert={invertLeft}
            soloHelp={HELP.soloLeft}
            invertHelp={HELP.invertLeft}
            onSolo={toggleSoloLeft}
            onInvert={() => setInvertLeft(!invertLeft)}
          />
        </div>
        <div style={connector('top')} />
      </div>
      {/* Seam pill: pan link + whole-chain swap, or the MONO chip when the
          rig sums the chains (linking pans and swapping lanes matter little
          to a mono blend, and the chip describes the lanes' joint output
          from the same spot). The inner footprint is fixed to the two icon
          boxes so both faces share one oval. */}
      <div
        {...(monoSum ? helpProps(HELP.monoSum) : {})}
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: '4rem',
          border: BORDER,
          borderRadius: '9999rem',
          padding: '3rem 5rem',
        }}
      >
        {monoSum ? (
          <div
            style={{
              width: `${ICON_BOX_SIZE * 2 + 4}rem`,
              height: `${ICON_BOX_SIZE}rem`,
              display: 'grid',
              placeItems: 'center',
              color: MUTED,
              fontSize: '11rem',
              fontFamily: FONT_MONO,
              lineHeight: 1,
            }}
          >
            <span className="cap-trim">MONO</span>
          </div>
        ) : (
          <>
            <ChromeIconButton
              tone="link"
              on={linked}
              help={HELP.panLink}
              onClick={handleToggleLink}
              offsetY={0}
            >
              <Link size={ICON_SIZE} style={{ transform: 'rotate(0deg)' }} />
            </ChromeIconButton>
            <ChromeIconButton help={HELP.swapChains} onClick={swapChains} offsetY={0}>
              <ArrowUpDown size={ICON_SIZE} />
            </ChromeIconButton>
          </>
        )}
      </div>
      <div style={knobRegion}>
        <div style={connector('bottom')} />
        <div style={panKnobWrap}>
          <div className={uiOffClass(monoSum)} style={panOffWrap} {...panOffHelp}>
            <KnobControl
              label="Pan R"
              value={panRight}
              onChange={handlePanRight}
              variant="panRight"
              min={0.5}
              max={1}
              size={KNOB_SIZE_SECONDARY}
              thumb="secondary"
              scale={PAN_RIGHT_SCALE}
              defaultValue={1}
              help={HELP.panRight}
              labelBright
              onDragStateChange={onPanRightDrag}
            />
          </div>
          <PanRailChips
            solo={soloRight}
            invert={invertRight}
            soloHelp={HELP.soloRight}
            invertHelp={HELP.invertRight}
            onSolo={toggleSoloRight}
            onInvert={() => setInvertRight(!invertRight)}
          />
        </div>
        <div style={spacer} />
      </div>
    </div>
  );
};

/**
 * The two-lane elbow of an active branch: a vertical drop from the trunk
 * lane's tap gap to the branch lane's row, plus the short horizontal stub
 * into the branch lane's first tile, using the same 1px hairlines as the ghost rail.
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
          left: `${x}rem`,
          top: `${topLaneCenter}rem`,
          width: '2rem',
          height: `${bottomLaneCenter - topLaneCenter}rem`,
          transform: 'translateX(-50%)',
        }}
      />
      <div
        style={{
          ...line,
          left: `${x}rem`,
          top: `${stubY}rem`,
          width: `${TILE_GAP / 2}rem`,
          height: '2rem',
          transform: 'translateY(-50%)',
        }}
      />
    </>
  );
};

/** Fade the lanes out under the gutters as they scroll, so content slides
    behind a smooth ramp to the background instead of hard-clipping. */
export const EdgeFade: React.FC<{ side: 'left' | 'right' }> = ({ side }) => (
  <div
    style={{
      position: 'absolute',
      top: 0,
      bottom: 0,
      // Overhang the outer edge by a design px: at fractional UI scales the
      // scrollport's clip edge and this overlay can round to different
      // device pixels, which would leave a subpixel strip of content visible
      // just past the fade. The overhang end is solid black over the black
      // background, so it never shows.
      [side]: '-1rem',
      width: `${EDGE_FADE_WIDTH + 1}rem`,
      background: `linear-gradient(to ${side === 'left' ? 'right' : 'left'}, #000000, rgba(0, 0, 0, 0))`,
      pointerEvents: 'none',
      zIndex: 3,
    }}
  />
);
