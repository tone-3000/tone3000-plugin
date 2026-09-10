import React, { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { getUiScale, rem } from '../hooks/useUiScale';
import { DragDropProvider } from '@dnd-kit/react';
import { isSortable } from '@dnd-kit/react/sortable';
import { KeyboardSensor, PointerActivationConstraints, PointerSensor } from '@dnd-kit/dom';
import type {
  DragDropManager,
  DragEndEvent,
  DragOverEvent,
  DragStartEvent,
  Sensors,
} from '@dnd-kit/dom';
import { arrayMove } from '@dnd-kit/helpers';
import { ChainBlock } from './ChainBlock';
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
import { useHorizontalWheelScroll } from '../hooks/useHorizontalWheelScroll';
import { FONT_MONO, WHITE } from './theme';
import type { ChainBranch, ChainItem, ChainSide, ToneBlock } from '../types/chain';
import { isInsertSlot } from '../types/chain';

/**
 * Chain gallery: blocks render as square image tiles in horizontal,
 * left-to-right lanes over a static ghost rail of plus circles joined by
 * connector lines. Dragging a
 * tile away reveals the rail behind its slot. (Lane internals live in
 * GalleryLane.tsx; this component owns the drag orchestration.)
 *
 * Mono shows one lane; stereo shows both L/R lanes in a single shared
 * scroll area with the pan/link/swap rail on the left. One drag context
 * spans both lanes and the lane lists are mirrored into optimistic local
 * state, so cross-lane drags reflow the target lane live (onDragOver) and
 * drops land without any snap-back while the native roundtrip completes.
 * Tap/click opens the detail takeover; drag a tile to reorder.
 */

/**
 * The block whose detail takeover is open, persisted so it survives this
 * component unmounting while the tone browser (and its OAuth redirect) is up.
 * Cleared from Plugin on preset load so a remount lands on the gallery.
 */
export const DETAIL_BLOCK_STORAGE_KEY = 't3k.detailBlockId';

interface ChainViewProps {
  /** Left lane (the only lane in mono mode). */
  chain: ChainItem[];
  /** Right lane, or null while mono. */
  chainRight: ChainItem[] | null;
  /** Active branch (stereo only), or null when the chains are independent. */
  branch: ChainBranch | null;
  /** Stereo chains on a rig that can't reproduce stereo: native sums them
      to mono. The pan rail dims its pans and shows the MONO chip. */
  monoSum: boolean;
  /** Whether the native block clipboard holds a copied block (enables Paste
      on insert slots). Survives preset switches: the clipboard snapshot is
      self-contained, not a reference into the current chain. */
  canPaste: boolean;
  sampleRate: number;
  /** Default NAM A2 size for new blocks; the detail card's size chip only
      shows when a block differs from it. */
  namSlimSizeDefault: number;
  /** Block info view: drop the meter-band bottom pad so scroll reaches the faceplate. */
  onFillToFaceplate?: (fill: boolean) => void;
  /** Bumped on preset load so an open detail takeover returns to the gallery. */
  returnToGallery?: number;
}

/** Design-px of travel before a drag engages, so tap/click stays a click.
    Scaled to real px per gesture so the feel tracks the rendered tile size. */
const GALLERY_DRAG_DISTANCE_PX = 6;

const sensors: Sensors = [
  // Distance-only activation (the stock constraints add a 200ms hold trigger,
  // which would turn a slow click-to-open into a drag). The sensor's default
  // guard already keeps buttons and other interactive chrome from starting
  // drags, so power/swap/trash stay clicks.
  PointerSensor.configure({
    activationConstraints: () => [
      new PointerActivationConstraints.Distance({
        value: GALLERY_DRAG_DISTANCE_PX * getUiScale(),
      }),
    ],
  }),
  // Stock keyboard sorting: Space or Enter on a focused tile picks it up,
  // arrows snap it one slot per press (the sortable's SortableKeyboardPlugin
  // owns the targeting), Space/Enter drops, Escape cancels. A grab can only
  // start on the focused tile, so this stays intentional: Space/Enter
  // anywhere else still falls through to the host DAW (see keyPassthrough.ts).
  KeyboardSensor,
];

type Lanes = Record<ChainSide, ChainItem[]>;

/** Scroll-restore target queued for the next return to the gallery: either a
    blockId (explicit Back — the block still exists, see onBack) or a lane +
    index (the open block vanished out from under us — trash, undo, redo...
    see the scroll-restore effect in ChainView). */
type PendingScroll = { kind: 'id'; blockId: string } | { kind: 'index'; side: ChainSide; index: number };

/** Id of the ⌥-duplicate stand-in: the inert copy of the dragged block that
    holds its home slot while the standard drag machinery runs untouched. */
const DUP_STAND_IN_ID = '__duplicate-stand-in__';

export const ChainView: React.FC<ChainViewProps> = ({
  chain,
  chainRight,
  branch,
  monoSum,
  canPaste,
  sampleRate,
  namSlimSizeDefault,
  onFillToFaceplate,
  returnToGallery = 0,
}) => {
  const actions = useChainActions();
  const wheelScrollRef = useHorizontalWheelScroll<HTMLDivElement>();
  // Persisted so the detail takeover survives this component unmounting: a
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
  // Preset load (Plugin) bumps this while we may be unmounted under the tuner
  // or tone browser; skip 0 so a restored detail after OAuth still opens.
  useEffect(() => {
    if (returnToGallery) setDetailBlockId(null);
  }, [returnToGallery]);
  /** The item under drag; drives the DragOverlay ghost. */
  const [activeDrag, setActiveDrag] = useState<ChainItem | null>(null);

  /** ⌥ held during the current drag; the drop duplicates instead of moving. */
  const altDragRef = useRef(false);

  /**
   * Optimistic mirror of both lanes. Drag gestures mutate this immediately
   * (live cross-lane reflow via onDragOver, final order on drop) so nothing
   * snaps back while the native mutation + resync roundtrip completes; it
   * resyncs from props whenever native reports a new state and no drag is
   * in flight.
   */
  const [lanes, setLanes] = useState<Lanes>({ left: chain, right: chainRight ?? [] });
  const draggingRef = useRef(false);

  // Resync the optimistic lanes only when native actually reports new state
  // (and no drag is in flight). `lanes` must NOT be a dependency here: an
  // earlier version included it and unconditionally set a fresh object, which
  // re-triggered itself in a silent render loop.
  useEffect(() => {
    if (!draggingRef.current) setLanes({ left: chain, right: chainRight ?? [] });
  }, [chain, chainRight]);

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
   * dragged block pinned at its home slot. The standard drag machinery
   * (traveling hole, parting neighbors, drop index) runs completely
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
  // drop bare modifier keydowns, see KnobControl) with key events for
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

  const handleDragStart = (event: DragStartEvent, manager: DragDropManager) => {
    draggingRef.current = true;
    const id = String(event.operation.source?.id);
    setActiveDrag([...lanes.left, ...lanes.right].find((i) => i.blockId === id) ?? null);
    // Seed from the press that started the drag; the tracker effect keeps it
    // live from here (and inserts the stand-in once activeDrag lands).
    const activator = manager.dragOperation.activatorEvent;
    altDragRef.current = activator instanceof PointerEvent && activator.altKey;
  };

  // Live cross-lane reflow: as the drag crosses into the other lane, move
  // the dragged item into it so that lane parts to make room, exactly like a
  // same-lane sort. Handled here (with the default optimistic cross-group
  // move suppressed) because the built-in resolves before/after with
  // vertical-list math; these lanes are horizontal, so which side of the
  // hovered tile the block lands on must follow the dragged tile's center x.
  // Same-lane sorting stays with the built-in OptimisticSortingPlugin.
  const handleDragOver = (event: DragOverEvent, manager: DragDropManager) => {
    const { source, target } = event.operation;
    if (!source || !target) return;
    const activeId = String(source.id);
    const from = laneOf(activeId);
    const to = laneOf(String(target.id));
    if (!from || !to || from === to) return;
    event.preventDefault();

    // Insert slots are lane anchors and stay put.
    const item = lanes[from].find((i) => i.blockId === activeId);
    if (!item || isInsertSlot(item)) return;

    // Land after the hovered tile when the dragged tile's center has passed
    // the hovered tile's center.
    const dragged = manager.dragOperation.shape?.current.center;
    const landAfter = dragged != null && target.shape != null && dragged.x > target.shape.center.x;

    setLanes((prev) => {
      const fromItems = prev[from].filter((i) => i.blockId !== activeId);
      const toItems = [...prev[to]];
      const overIndex = toItems.findIndex((i) => i.blockId === String(target.id));
      const insertIndex = overIndex === -1 ? toItems.length : overIndex + (landAfter ? 1 : 0);
      toItems.splice(insertIndex, 0, item);
      return { ...prev, [from]: fromItems, [to]: toItems };
    });
    // Same stabilization OptimisticSortingPlugin applies after its own moves:
    // park the drop target on the source and hold collision detection until
    // the reflowed layout has rendered, so stale rects can't bounce the item
    // straight back across the lanes.
    manager.collisionObserver.disable();
    void manager.actions.setDropTarget(source.id).then(() => {
      manager.collisionObserver.enable();
    });
  };

  const handleDragEnd = (event: DragEndEvent) => {
    draggingRef.current = false;
    setActiveDrag(null);
    const duplicating = altDragRef.current;
    altDragRef.current = false;
    const { source, target } = event.operation;
    if (event.canceled || !target || !isSortable(source)) {
      resetLanes();
      return;
    }

    const activeId = String(source.id);
    const side = laneOf(activeId);
    if (!side) return;

    // Final same-lane placement: cross-lane moves already landed in
    // onDragOver, and source.index is the optimistic index the drag settled
    // on (the sortable plugin keeps it live during the gesture).
    let laneItems = lanes[side];
    const oldIndex = laneItems.findIndex((i) => i.blockId === activeId);
    const newIndex = Math.min(source.index, laneItems.length - 1);
    if (oldIndex !== -1 && oldIndex !== newIndex) {
      laneItems = arrayMove(laneItems, oldIndex, newIndex);
      setLanes((prev) => ({ ...prev, [side]: laneItems }));
    }
    const finalIndex = laneItems.findIndex((i) => i.blockId === activeId);

    // ⌥-drop: same layout, same index math; the mutation is a clone instead
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

  // Resolve the detail block across both lanes; it can disappear underneath
  // us (undo, trash from the detail header, redo of a delete...), in which
  // case we fall back to the gallery.
  const detailBlock =
    detailBlockId != null
      ? ([...chain, ...(chainRight ?? [])].find(
          (item): item is ToneBlock => !isInsertSlot(item) && item.blockId === detailBlockId
        ) ?? null)
      : null;

  const stereo = chainRight != null;
  const tileSize = stereo ? STEREO_TILE_SIZE : TILE_SIZE;

  // Branched layout: the branch lane starts at the trunk's tap gap, so its
  // row is indented past the whole trunk prefix (matching the signal flow:
  // its input *is* that prefix's output). Resolved against the optimistic
  // lane state; a stale tap id (mid-resync after the tapped block moved)
  // renders as independent lanes until native's cleared state arrives.
  // Computed here (ahead of the detail-view early return) because the
  // scroll-restore effect below needs it too, and hooks can't follow a
  // conditional return.
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

  // Scroll-restore target queued for the next return to the gallery; consumed
  // synchronously by the effect below, so no render should ever observe it.
  const pendingScrollTargetRef = useRef<PendingScroll | null>(null);

  // The open detail block's last-known lane + index, kept fresh on every
  // render it's still resolvable (a plain ref write during render — cheap,
  // and there's no cleaner hook for "remember the last real value before a
  // prop-driven change replaces it"). If the block then vanishes from
  // underneath us, there's nothing left to look it up by id, so the effect
  // below falls back to this instead.
  const lastDetailPositionRef = useRef<{ side: ChainSide; index: number } | null>(null);
  if (detailBlock != null) {
    const side = laneOf(detailBlock.blockId);
    const index =
      side != null ? lanes[side].findIndex((i) => i.blockId === detailBlock.blockId) : -1;
    if (side != null && index !== -1) lastDetailPositionRef.current = { side, index };
  }

  const galleryScrollElRef = useRef<HTMLDivElement | null>(null);
  const setGalleryScrollEl = useCallback(
    (el: HTMLDivElement | null) => {
      wheelScrollRef(el);
      galleryScrollElRef.current = el;
    },
    [wheelScrollRef]
  );

  // Center the just-closed (or just-vanished) block's tile instead of
  // leaving the gallery scrolled to wherever a freshly mounted scroller
  // defaults (issue #82): the gallery's scroll div unmounts while the detail
  // takeover is open (see useHorizontalWheelScroll), so there's no prior
  // scrollLeft to restore. Recomputing the tile's position (rather than
  // replaying a raw offset) also survives the chain reshaping while the
  // takeover was open (the block moved, or a preceding block was
  // deleted/inserted).
  useLayoutEffect(() => {
    if (detailBlock != null) return;

    // detailBlockId only reaches null a step ahead of us, via onBack itself
    // (which seeds pendingScrollTargetRef in the same breath it clears this
    // state). Landing here with detailBlockId still set means the block the
    // takeover was open on disappeared out from under us instead — trash,
    // undo, redo, anything native-initiated — so there's no explicit Back to
    // rely on: fall back to its last known position, and drop the now-
    // dangling id (otherwise it lingers in state and sessionStorage forever).
    if (detailBlockId != null) {
      setDetailBlockId(null);
      if (lastDetailPositionRef.current != null) {
        pendingScrollTargetRef.current = { kind: 'index', ...lastDetailPositionRef.current };
      }
    }

    const target = pendingScrollTargetRef.current;
    const el = galleryScrollElRef.current;
    if (target == null || el == null) return;
    pendingScrollTargetRef.current = null;

    let side: ChainSide;
    let index: number;
    if (target.kind === 'id') {
      const resolvedSide = laneOf(target.blockId);
      if (resolvedSide == null) return;
      side = resolvedSide;
      index = lanes[resolvedSide].findIndex((i) => i.blockId === target.blockId);
      if (index === -1) return;
    } else {
      side = target.side;
      // The vacated slot may now be past the end (e.g. the removed block was
      // the last real one, leaving only trailing insert slots).
      index = Math.min(target.index, lanes[side].length - 1);
      if (index < 0) return;
    }

    const indent =
      branchLayout != null && side !== branchLayout.trunkSide ? branchLayout.indentPx : 0;
    const centerDesignPx = EDGE_FADE_WIDTH + indent + index * (tileSize + TILE_GAP) + tileSize / 2;
    const centerPx = centerDesignPx * getUiScale();
    const max = el.scrollWidth - el.clientWidth;
    el.scrollLeft = Math.max(0, Math.min(max, centerPx - el.clientWidth / 2));
    // laneOf reads the same `lanes` this effect depends on; branchLayout and
    // tileSize are derived from lanes/branch/chainRight above.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [detailBlock, detailBlockId, lanes, branchLayout, tileSize]);

  if (detailBlock) {
    // Another enabled+loaded NAM after this block in its lane. This mirrors the
    // DSP's lastNamIndex scan (Processor.cpp): with calibration on, such a
    // block hands off at calibrated output level instead of normalizing.
    const detailLane = chain.some((item) => item.blockId === detailBlock.blockId)
      ? chain
      : (chainRight ?? []);
    const detailIndex = detailLane.findIndex((item) => item.blockId === detailBlock.blockId);
    const namDownstream = detailLane
      .slice(detailIndex + 1)
      .some(
        (item): item is ToneBlock =>
          !isInsertSlot(item) &&
          item.tone.format?.toLowerCase() === 'nam' &&
          item.loaded &&
          item.params.enabled
      );

    return (
      <div
        style={{
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          height: '100%',
          // Top-align under the shared 24px middle-band pad (Plugin); the
          // card bottom then sits 24px above the faceplate when the column
          // matches the meter height (Figma). Info view scrolls ← BLOCK + card.
          justifyContent: 'flex-start',
          boxSizing: 'border-box',
        }}
      >
        <ChainBlock
          block={detailBlock}
          namDownstream={namDownstream}
          sampleRate={sampleRate}
          namSlimSizeDefault={namSlimSizeDefault}
          onBack={() => {
            pendingScrollTargetRef.current = { kind: 'id', blockId: detailBlock.blockId };
            setDetailBlockId(null);
          }}
          onFillToFaceplate={onFillToFaceplate}
        />
      </div>
    );
  }

  const lane = (side: ChainSide) => (
    <div
      style={{
        marginLeft:
          branchLayout != null && side !== branchLayout.trunkSide
            ? `${branchLayout.indentPx}rem`
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
        onPasteBlock={canPaste ? (index) => actions.pasteBlock(side, index) : null}
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
        padding: '0 24rem',
      }}
    >
      {stereo && <StereoPanRail monoSum={monoSum} />}
      <DragDropProvider
        sensors={sensors}
        onDragStart={handleDragStart}
        onDragOver={handleDragOver}
        onDragEnd={handleDragEnd}
      >
        {/* One shared scroll area: both lanes pan together, fading out under
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
                left: rem(EDGE_FADE_WIDTH),
                zIndex: 1,
                pointerEvents: 'none',
                fontFamily: FONT_MONO,
                fontSize: '16rem',
                fontWeight: 400,
                letterSpacing: 'normal',
                textTransform: 'uppercase',
                color: WHITE,
              }}
            >
              Signal Chain
            </span>
          )}
          <div
            ref={setGalleryScrollEl}
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
                gap: `${LANE_GAP}rem`,
                width: 'max-content',
                minWidth: '100%',
                padding: `0 ${EDGE_FADE_WIDTH}rem`,
                boxSizing: 'border-box',
                // No transform on this wrapper: a transformed ancestor becomes
                // the containing block for position:fixed descendants, and
                // dnd-kit positions the dragged tile in fixed viewport
                // coordinates. In webviews without top-layer (popover)
                // promotion the tile would render offset by this box's origin,
                // a big down-right jump at pickup in DAW hosts.
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
      </DragDropProvider>
    </div>
  );
};
