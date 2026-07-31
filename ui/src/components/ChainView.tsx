import React, { useEffect, useRef, useState } from 'react';
import {
  DndContext,
  DragOverlay,
  closestCenter,
  pointerWithin,
  KeyboardSensor,
  useSensor,
  useSensors,
} from '@dnd-kit/core';
import type {
  CollisionDetection,
  DragEndEvent,
  DragOverEvent,
  DragStartEvent,
} from '@dnd-kit/core';
import { arrayMove, sortableKeyboardCoordinates } from '@dnd-kit/sortable';
import { ChainBlock } from './ChainBlock';
import { GalleryTileGhost } from './GalleryBlock';
import { GalleryPointerSensor, GALLERY_DRAG_DISTANCE_PX } from './galleryPointerSensor';
import {
  BranchElbow,
  EdgeFade,
  GalleryLane,
  LANE_GAP,
  STEREO_TILE_SIZE,
  StereoPanRail,
  TILE_GAP,
  TILE_SIZE,
  EDGE_FADE_WIDTH,
  gapCenterX,
} from './GalleryLane';
import { useChainActions } from '../hooks/useChainActions';
import { useCopiedBlockId } from '../hooks/useBlockClipboard';
import { WHITE } from './theme';
import type { ChainBranch, ChainItem, ChainSide, ToneBlock } from '../types/chain';
import { isInsertSlot } from '../types/chain';

/**
 * Chain gallery: blocks render as square image tiles in horizontal,
 * left-to-right lanes over a static ghost rail of plus circles joined by
 * connector lines — the old vertical chain's link UI, rotated. Dragging a
 * tile away reveals the rail behind its slot. (Lane internals live in
 * GalleryLane.tsx; this component owns the drag orchestration.)
 *
 * Mono shows one lane; stereo shows both L/R lanes in a single shared
 * scroll area with the pan/link/swap rail on the left. One drag context
 * spans both lanes and the lane lists are mirrored into optimistic local
 * state, so cross-lane drags reflow the target lane live (onDragOver) and
 * drops land without any snap-back while the native roundtrip completes.
 * Tap/click opens the detail takeover; drag the tile (or grip) to reorder.
 */

/**
 * The block whose detail takeover is open, persisted so it survives this
 * component unmounting while the tone browser (and its OAuth redirect) is up.
 */
const DETAIL_BLOCK_STORAGE_KEY = 't3k.detailBlockId';

interface ChainViewProps {
  /** Left lane (the only lane in mono mode). */
  chain: ChainItem[];
  /** Right lane, or null while mono. */
  chainRight: ChainItem[] | null;
  /** Active branch (stereo only), or null when the chains are independent. */
  branch: ChainBranch | null;
  sampleRate: number;
}

/**
 * Prefer the tile actually under the pointer; only fall back to nearest-
 * center when the pointer is in a gap. closestCenter alone oscillates during
 * cross-lane drags: right after a move shifts the layout, stale rects can
 * make the "nearest" target flip back to the old lane.
 */
const galleryCollisionDetection: CollisionDetection = (args) => {
  const pointerCollisions = pointerWithin(args);
  return pointerCollisions.length > 0 ? pointerCollisions : closestCenter(args);
};

type Lanes = Record<ChainSide, ChainItem[]>;

/** Id of the ⌥-duplicate stand-in — the inert copy of the dragged block that
    holds its home slot while the standard drag machinery runs untouched. */
const DUP_STAND_IN_ID = '__duplicate-stand-in__';

export const ChainView: React.FC<ChainViewProps> = ({
  chain,
  chainRight,
  branch,
  sampleRate,
}) => {
  const actions = useChainActions();
  // Persisted so the detail takeover survives this component unmounting — a
  // swap from the detail view opens the tone browser (which replaces the whole
  // chain view, and may bounce through the tone3000.com OAuth redirect). The
  // swap keeps the same blockId, so we reopen the detail view for it on return.
  // Cleared when the user backs out, so gallery-initiated swaps land on the
  // gallery, not a stale detail view.
  const [detailBlockId, setDetailBlockId] = useState<string | null>(() =>
    sessionStorage.getItem(DETAIL_BLOCK_STORAGE_KEY)
  );
  useEffect(() => {
    if (detailBlockId) sessionStorage.setItem(DETAIL_BLOCK_STORAGE_KEY, detailBlockId);
    else sessionStorage.removeItem(DETAIL_BLOCK_STORAGE_KEY);
  }, [detailBlockId]);
  /** The item under drag — drives the DragOverlay ghost. */
  const [activeDrag, setActiveDrag] = useState<ChainItem | null>(null);

  /** ⌥ held during the current drag — the drop duplicates instead of moving. */
  const altDragRef = useRef(false);

  // Copy/paste clipboard: paste is only offered while the copied block still
  // exists (native resolves the data from the id, so a deleted source can't
  // be pasted).
  const copiedBlockId = useCopiedBlockId();

  /**
   * Optimistic mirror of both lanes. Drag gestures mutate this immediately
   * (live cross-lane reflow via onDragOver, final order on drop) so nothing
   * snaps back while the native mutation + resync roundtrip completes; it
   * resyncs from props whenever native reports a new state and no drag is
   * in flight.
   */
  const [lanes, setLanes] = useState<Lanes>({ left: chain, right: chainRight ?? [] });
  const draggingRef = useRef(false);
  /**
   * Set for one frame after a cross-lane move. dnd-kit fires another
   * onDragOver as soon as the layout shifts, before it has re-measured — and
   * with stale rects the nearest target can resolve back to the old lane,
   * bouncing the item between lanes forever (React's "maximum update depth"
   * crash). Cross-lane moves are skipped until the next animation frame,
   * when the new measurements are in.
   */
  const justCrossedRef = useRef(false);

  // Resync the optimistic lanes only when native actually reports new state
  // (and no drag is in flight). `lanes` must NOT be a dependency here — the
  // old version included it and unconditionally set a fresh object, which
  // re-triggered itself in a silent render loop.
  useEffect(() => {
    if (!draggingRef.current) setLanes({ left: chain, right: chainRight ?? [] });
  }, [chain, chainRight]);

  const sensors = useSensors(
    // A few px of travel before a drag engages: plain clicks (open detail,
    // tile buttons) stay clicks. GalleryPointerSensor also ignores presses
    // on interactive chrome so power/swap/trash stay clicks.
    useSensor(GalleryPointerSensor, {
      activationConstraint: { distance: GALLERY_DRAG_DISTANCE_PX },
    }),
    useSensor(KeyboardSensor, { coordinateGetter: sortableKeyboardCoordinates })
  );

  /** Lane containing the id in the optimistic local state. */
  const laneOf = (id: string): ChainSide | null => {
    if (lanes.left.some((item) => item.blockId === id)) return 'left';
    if (lanes.right.some((item) => item.blockId === id)) return 'right';
    return null;
  };
  /** Lane containing the id per native state (the pre-drag origin). */
  const originLaneOf = (id: string): ChainSide | null => {
    if (chain.some((item) => item.blockId === id)) return 'left';
    if (chainRight?.some((item) => item.blockId === id)) return 'right';
    return null;
  };

  const resetLanes = () => setLanes({ left: chain, right: chainRight ?? [] });

  /**
   * Insert (or remove) the ⌥-duplicate stand-in: an inert copy of the
   * dragged block pinned at its home slot. The standard drag machinery —
   * traveling hole, parting neighbors, drop index — runs completely
   * untouched; with the home slot visibly occupied, the exact same gesture
   * reads as pulling a *copy* out instead of moving the block. Rebuilt from
   * native state so toggling ⌥ mid-drag also undoes any optimistic
   * cross-lane reflow (the next dragOver re-parts the target lane).
   */
  const setDuplicateStandIn = (item: ChainItem | null) =>
    setLanes(() => {
      const left = [...chain];
      const right = [...(chainRight ?? [])];
      if (item != null) {
        const lane = left.some((i) => i.blockId === item.blockId) ? left : right;
        const index = lane.findIndex((i) => i.blockId === item.blockId);
        if (index !== -1) lane.splice(index, 0, { ...item, blockId: DUP_STAND_IN_ID });
      }
      return { left, right };
    });

  // ⌥ tracking rides pointermove (drags move constantly, and the webview can
  // drop bare modifier keydowns — see KnobControl) with key events for
  // in-place toggles. Tone blocks only; inserts have nothing to duplicate.
  useEffect(() => {
    if (activeDrag == null || isInsertSlot(activeDrag)) return;
    if (altDragRef.current) setDuplicateStandIn(activeDrag); // ⌥ down at drag start
    const track = (e: PointerEvent | KeyboardEvent) => {
      if (e.altKey === altDragRef.current) return;
      altDragRef.current = e.altKey;
      setDuplicateStandIn(e.altKey ? activeDrag : null);
    };
    window.addEventListener('pointermove', track);
    window.addEventListener('keydown', track);
    window.addEventListener('keyup', track);
    return () => {
      window.removeEventListener('pointermove', track);
      window.removeEventListener('keydown', track);
      window.removeEventListener('keyup', track);
    };
    // setDuplicateStandIn closes over chain/chainRight; those are stable for
    // the life of a drag (native doesn't push mid-gesture).
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [activeDrag]);

  const handleDragStart = (event: DragStartEvent) => {
    draggingRef.current = true;
    const id = String(event.active.id);
    setActiveDrag([...lanes.left, ...lanes.right].find((i) => i.blockId === id) ?? null);
    // Seed from the press that started the drag; the tracker effect keeps it
    // live from here (and inserts the stand-in once activeDrag lands).
    altDragRef.current = (event.activatorEvent as PointerEvent | null)?.altKey === true;
  };

  // Live cross-lane reflow: as the pointer crosses into the other lane, move
  // the dragged item into it so that lane parts to make room, exactly like a
  // same-lane sort. Which side of the hovered tile it lands on follows the
  // dragged tile's center.
  const handleDragOver = (event: DragOverEvent) => {
    const { active, over } = event;
    if (!over) return;
    const activeId = String(active.id);
    const overId = String(over.id);
    const from = laneOf(activeId);
    const to = laneOf(overId);
    if (!from || !to || from === to) return;
    if (justCrossedRef.current) return;

    // Insert slots are lane anchors and stay put.
    const item = lanes[from].find((i) => i.blockId === activeId);
    if (!item || isInsertSlot(item)) return;

    // Land after the hovered tile when the dragged tile's center has passed
    // the hovered tile's center.
    const activeRect = active.rect.current.translated;
    const landAfter =
      activeRect != null &&
      activeRect.left + activeRect.width / 2 > over.rect.left + over.rect.width / 2;

    justCrossedRef.current = true;
    // Clear the guard once dnd-kit has re-measured the shifted layout.
    requestAnimationFrame(() => {
      justCrossedRef.current = false;
    });
    setLanes((prev) => {
      const fromItems = prev[from].filter((i) => i.blockId !== activeId);
      const toItems = [...prev[to]];
      const overIndex = toItems.findIndex((i) => i.blockId === overId);
      const insertIndex = overIndex === -1 ? toItems.length : overIndex + (landAfter ? 1 : 0);
      toItems.splice(insertIndex, 0, item);
      return { ...prev, [from]: fromItems, [to]: toItems };
    });
  };

  const handleDragEnd = (event: DragEndEvent) => {
    draggingRef.current = false;
    setActiveDrag(null);
    const duplicating = altDragRef.current;
    altDragRef.current = false;
    const { active, over } = event;
    if (!over) {
      resetLanes();
      return;
    }

    const activeId = String(active.id);
    const overId = String(over.id);
    const side = laneOf(activeId);
    if (!side) return;

    // Final same-lane placement (cross-lane moves already happened in
    // onDragOver, so active and over share a lane by now).
    let laneItems = lanes[side];
    const oldIndex = laneItems.findIndex((i) => i.blockId === activeId);
    const newIndex = laneItems.findIndex((i) => i.blockId === overId);
    if (oldIndex !== -1 && newIndex !== -1 && oldIndex !== newIndex) {
      laneItems = arrayMove(laneItems, oldIndex, newIndex);
      setLanes((prev) => ({ ...prev, [side]: laneItems }));
    }
    const finalIndex = laneItems.findIndex((i) => i.blockId === activeId);

    // ⌥-drop: same layout, same index math — the mutation is a clone instead
    // of a move. The stand-in holds the home slot, so `finalIndex` already
    // counts the original staying put; the optimistic lanes match the
    // post-clone chain pixel-for-pixel until the resync swaps in real ids.
    if (duplicating && finalIndex !== -1 && !isInsertSlot(laneItems[finalIndex])) {
      actions.duplicateBlock(activeId, side, finalIndex);
      return;
    }

    // Commit to native: a lane change is one moveBlock (exact final index);
    // a same-lane shuffle is one reorder. The chainChanged resync converges
    // the optimistic state.
    const origin = originLaneOf(activeId);
    if (origin && origin !== side) {
      actions.moveBlock(activeId, side, finalIndex);
      return;
    }
    const nativeIds = (side === 'left' ? chain : (chainRight ?? [])).map((i) => i.blockId);
    const localIds = laneItems.map((i) => i.blockId);
    if (nativeIds.join() !== localIds.join()) actions.reorderBlocks(localIds);
  };

  const handleDragCancel = () => {
    draggingRef.current = false;
    altDragRef.current = false;
    setActiveDrag(null);
    resetLanes();
  };

  // Resolve the detail block across both lanes; it can disappear underneath
  // us (undo, trash from the detail header), in which case we fall back to
  // the gallery.
  const detailBlock =
    detailBlockId != null
      ? ([...chain, ...(chainRight ?? [])].find(
          (item): item is ToneBlock => !isInsertSlot(item) && item.blockId === detailBlockId
        ) ?? null)
      : null;

  if (detailBlock) {
    return (
      <div
        style={{
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          height: '100%',
          // Top-align under the shared 24px middle-band pad (Plugin); the
          // card bottom then sits 24px above the faceplate when the column
          // matches the meter height (Figma).
          justifyContent: 'flex-start',
          boxSizing: 'border-box',
        }}
      >
        <ChainBlock
          block={detailBlock}
          sampleRate={sampleRate}
          onBack={() => setDetailBlockId(null)}
        />
      </div>
    );
  }

  const stereo = chainRight != null;
  const tileSize = stereo ? STEREO_TILE_SIZE : TILE_SIZE;

  // Validated against the live chain each render — a copied-then-deleted
  // block leaves Paste disabled rather than pasting a ghost.
  const pasteSourceId =
    copiedBlockId != null &&
    [...chain, ...(chainRight ?? [])].some(
      (item) => !isInsertSlot(item) && item.blockId === copiedBlockId
    )
      ? copiedBlockId
      : null;

  // Branched layout: the branch lane starts at the trunk's tap gap, so its
  // row is indented past the whole trunk prefix (matching the signal flow —
  // its input *is* that prefix's output). Resolved against the optimistic
  // lane state; a stale tap id (mid-resync after the tapped block moved)
  // renders as independent lanes until native's cleared state arrives.
  const branchLayout = (() => {
    if (!stereo || branch == null) return null;
    const tapIndex = lanes[branch.side].findIndex((i) => i.blockId === branch.afterBlockId);
    if (tapIndex === -1) return null;
    return {
      trunkSide: branch.side,
      indentPx: (tapIndex + 1) * (tileSize + TILE_GAP),
      tapGapX: gapCenterX(tapIndex + 1, tileSize),
    };
  })();

  const lane = (side: ChainSide) => (
    <div
      style={{
        marginLeft:
          branchLayout != null && side !== branchLayout.trunkSide
            ? `${branchLayout.indentPx}px`
            : 0,
        width: 'max-content',
      }}
    >
      <GalleryLane
        items={lanes[side]}
        tileSize={tileSize}
        stereo={stereo}
        onOpen={setDetailBlockId}
        onAdd={(insertBlockId) => actions.addModel(side, insertBlockId)}
        onPasteBlock={
          pasteSourceId != null
            ? (index) => actions.duplicateBlock(pasteSourceId, side, index)
            : null
        }
        side={side}
        branch={branchLayout != null ? branch : null}
        branchInteractive={stereo && activeDrag == null}
        onSetBranch={(afterBlockId) => actions.setBranch(side, afterBlockId)}
        onClearBranch={actions.clearBranch}
      />
    </div>
  );

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'row',
        alignItems: 'stretch',
        height: '100%',
        boxSizing: 'border-box',
        padding: '0 24px',
      }}
    >
      {stereo && <StereoPanRail />}
      <DndContext
        sensors={sensors}
        collisionDetection={galleryCollisionDetection}
        onDragStart={handleDragStart}
        onDragOver={handleDragOver}
        onDragEnd={handleDragEnd}
        onDragCancel={handleDragCancel}
      >
        {/* One shared scroll area — both lanes pan together, fading out under
            the edge gradients as they scroll. */}
        <div style={{ position: 'relative', flex: 1, minWidth: 0, display: 'flex' }}>
          {/* Mono-only section title. Absolutely positioned so it sits in the
              top-left dead space without shifting the vertically/horizontally
              centered lane. left matches the lane's EDGE_FADE_WIDTH inset so
              the label lines up with the first tile; top is 0 because Plugin
              already applies the shared 24px middle-band pad. */}
          {!stereo && (
            <span
              style={{
                position: 'absolute',
                top: 0,
                left: EDGE_FADE_WIDTH,
                zIndex: 1,
                pointerEvents: 'none',
                fontFamily: 'monospace',
                fontSize: '16px',
                fontWeight: 400,
                letterSpacing: '0.08em',
                textTransform: 'uppercase',
                color: WHITE,
              }}
            >
              Signal Chain
            </span>
          )}
          <div
            className="hide-scrollbar"
            style={{
              flex: 1,
              minWidth: 0,
              overflowX: 'auto',
              overflowY: 'hidden',
              display: 'flex',
              flexDirection: 'column',
              justifyContent: 'center',
            }}
          >
            <div
              style={{
                position: 'relative',
                display: 'flex',
                flexDirection: 'column',
                gap: `${LANE_GAP}px`,
                width: 'max-content',
                minWidth: '100%',
                padding: `0 ${EDGE_FADE_WIDTH}px`,
                boxSizing: 'border-box',
                // Vertical centering can leave the content on a half-pixel
                // boundary; keeping it on its own composited layer snaps it
                // to the pixel grid once, so tiles can't shift ~1px when a
                // drag or opacity fade promotes them to their own layers.
                transform: 'translateZ(0)',
              }}
            >
              {lane('left')}
              {stereo && lane('right')}
              {branchLayout != null && (
                <BranchElbow
                  x={EDGE_FADE_WIDTH + branchLayout.tapGapX}
                  tileSize={tileSize}
                  trunkOnTop={branchLayout.trunkSide === 'left'}
                />
              )}
            </div>
          </div>
          <EdgeFade side="left" />
          <EdgeFade side="right" />
        </div>
        {/* No drop animation: the default one measures the original element
            before the optimistic lane commit paints, so the ghost would fly
            back to the old slot before the tile appears at the new one. */}
        <DragOverlay dropAnimation={null}>
          {activeDrag && <GalleryTileGhost item={activeDrag} size={tileSize} stereo={stereo} />}
        </DragOverlay>
      </DndContext>
    </div>
  );
};
