import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  ArrowUpDown,
  Check,
  ChevronLeft,
  ChevronRight,
  GripVertical,
  MidiPort,
  Pencil,
  Plus,
  Save,
  Search,
  Trash2,
} from './icons';
import { IconButton } from './IconButton';
import { DragDropProvider } from '@dnd-kit/react';
import { isSortable, useSortable } from '@dnd-kit/react/sortable';
import { arrayMove } from '@dnd-kit/helpers';
import type { DragEndEvent } from '@dnd-kit/react';
import type { ActivePreset, PresetInfo } from '../types/chain';
import { useDismissable } from '../hooks/useDismissable';
import { IS_COARSE_POINTER } from '../hooks/useUiScale';
import { useToast } from './Toast';
import { HELP, helpProps } from './helpText';
import { BORDER, FONT_MONO, GRAY, SEGMENTED_TRACK } from './theme';
import { setPresetPcNumbersEnabled, usePresetPcNumbersEnabled } from './uiPreferences';

/**
 * Top-bar preset controls: ‹ name › pill, save and New buttons, with two
 * anchored panels: the save popover (name + save) and the preset browser
 * (search, user section with inline rename/delete, TONE3000 factory section,
 * and a reorder mode that swaps the row actions for a grip and drag-and-drop).
 *
 * Pure view: the list and all mutations come from usePresets, the active
 * preset rides the chain state poll. Prev/next walk the list in its shown
 * order (user section first, then factory, same as the native list), which
 * is also what MIDI program-change numbers follow. A MIDI-port toggle reveals
 * each row's PC number (persisted in localStorage); off by default.
 */

const MUTED = GRAY;
const PANEL_BG = '#141416';

const panelStyle: React.CSSProperties = {
  position: 'absolute',
  top: 'calc(100% + 10rem)',
  left: '-8rem',
  backgroundColor: PANEL_BG,
  border: BORDER,
  borderRadius: '14rem',
  padding: '16rem',
  zIndex: 200,
  boxSizing: 'border-box',
};

const inputStyle: React.CSSProperties = {
  width: '100%',
  boxSizing: 'border-box',
  backgroundColor: '#1C1C1E',
  border: BORDER,
  borderRadius: '10rem',
  color: '#ffffff',
  fontSize: '13rem',
  // Typed text and placeholders are body text: reset the global 600 default.
  fontWeight: 400,
  padding: '9rem 12rem',
  outline: 'none',
};

const sectionHeaderStyle: React.CSSProperties = {
  color: GRAY,
  fontSize: '14rem',
  fontWeight: 700,
  padding: '8rem 4rem',
};

const iconButtonStyle: React.CSSProperties = {
  background: 'transparent',
  border: 'none',
  color: '#ffffff',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  cursor: 'pointer',
  borderRadius: '4rem',
  padding: '5rem',
};

type PresetGroup = 'factory' | 'user';

interface PresetRowProps {
  preset: PresetInfo;
  index: number;
  group: PresetGroup;
  sortable: boolean;
  /** MIDI program change that loads this preset; undefined past PC 127. */
  pcNumber: number | undefined;
  isActive: boolean;
  isRenaming: boolean;
  renameValue: string;
  onRenameChange: (value: string) => void;
  onCommitRename: () => void;
  onCancelRename: () => void;
  onLoad: () => void;
  onStartRename: () => void;
  onDelete: () => void;
}

const PresetRow: React.FC<PresetRowProps> = ({
  preset,
  index,
  group,
  sortable,
  pcNumber,
  isActive,
  isRenaming,
  renameValue,
  onRenameChange,
  onCommitRename,
  onCancelRename,
  onLoad,
  onStartRename,
  onDelete,
}) => {
  const { ref, handleRef, isDragging } = useSortable({
    id: preset.id,
    index,
    group,
    // Rows only accept drops from their own section, so a user preset can
    // never be dragged into the factory list or vice versa.
    type: group,
    accept: group,
    disabled: !sortable,
  });

  return (
    <div
      ref={ref}
      style={{
        display: 'flex',
        alignItems: 'center',
        gap: '8rem',
        height: '32rem',
        padding: '0 4rem',
        opacity: isDragging ? 0.75 : 1,
      }}
    >
      <span style={{ width: '16rem', flexShrink: 0, display: 'flex', alignItems: 'center' }}>
        {isActive && <Check size={14} color="#ffffff" />}
      </span>
      {isRenaming ? (
        <input
          className="t3k-touch-field"
          autoFocus
          value={renameValue}
          onChange={(e) => onRenameChange(e.target.value)}
          onBlur={onCommitRename}
          onKeyDown={(e) => {
            if (e.key === 'Enter') onCommitRename();
            if (e.key === 'Escape') onCancelRename();
          }}
          style={{ ...inputStyle, padding: '4rem 8rem', borderRadius: '6rem', flex: 1 }}
        />
      ) : (
        <button
          onClick={onLoad}
          style={{
            flex: 1,
            background: 'transparent',
            border: 'none',
            textAlign: 'left',
            color: isActive ? '#ffffff' : MUTED,
            fontSize: '14rem',
            fontWeight: 400,
            cursor: 'pointer',
            padding: 0,
            overflow: 'hidden',
            textOverflow: 'ellipsis',
            whiteSpace: 'nowrap',
          }}
        >
          {preset.name}
        </button>
      )}
      {pcNumber !== undefined && (
        <span
          {...helpProps(HELP.presetPc)}
          style={{
            flexShrink: 0,
            color: MUTED,
            fontSize: '11rem',
            fontWeight: 400,
            fontFamily: FONT_MONO,
          }}
        >
          PC {pcNumber}
        </span>
      )}
      {sortable ? (
        <button
          ref={handleRef}
          type="button"
          aria-label="Reorder"
          {...helpProps(HELP.presetDrag)}
          style={{
            ...iconButtonStyle,
            padding: '3rem',
            flexShrink: 0,
            cursor: isDragging ? 'grabbing' : 'grab',
            // Keep the list from claiming the gesture on touch.
            touchAction: 'none',
          }}
        >
          <GripVertical size={14} />
        </button>
      ) : (
        !preset.factory &&
        !isRenaming && (
          <>
            <button
              onClick={onStartRename}
              {...helpProps(HELP.presetRename)}
              style={{ ...iconButtonStyle, padding: '3rem' }}
            >
              <Pencil size={13} />
            </button>
            <button
              onClick={onDelete}
              {...helpProps(HELP.presetDelete)}
              style={{ ...iconButtonStyle, padding: '3rem' }}
            >
              <Trash2 size={13} />
            </button>
          </>
        )
      )}
    </div>
  );
};

interface PresetBarProps {
  active: ActivePreset | null;
  presets: PresetInfo[];
  /** Greys out the New button: nothing differs from a fresh instance. */
  atDefault: boolean;
  onSave: (name: string) => Promise<{ id: string; name: string } | null>;
  onLoad: (id: string) => void;
  onRename: (id: string, name: string) => void;
  onDelete: (id: string) => void;
  /** N steps within the preset's section (negative = earlier). */
  onMove: (id: string, delta: number) => void;
  /** Clear the chain and reset every control to its default. */
  onReset: () => void;
}

type OpenPanel = 'none' | 'save' | 'browse';

export const PresetBar: React.FC<PresetBarProps> = ({
  active,
  presets,
  atDefault,
  onSave,
  onLoad,
  onRename,
  onDelete,
  onMove,
  onReset,
}) => {
  const [open, setOpen] = useState<OpenPanel>('none');
  const [saveName, setSaveName] = useState('');
  const [search, setSearch] = useState('');
  const [renamingId, setRenamingId] = useState<string | null>(null);
  const [renameValue, setRenameValue] = useState('');
  // Reorder mode: rows swap rename/delete for a grip handle. Drag only
  // makes sense on the full list, so grips hide while a search filter is on.
  const [reordering, setReordering] = useState(false);
  const showPcNumbers = usePresetPcNumbersEnabled();
  const toast = useToast();
  const containerRef = useRef<HTMLDivElement | null>(null);
  const closePanels = useCallback(() => setOpen('none'), []);
  useDismissable(open !== 'none', containerRef, closePanels);

  // Optimistic order while a drag is in flight / until native list refresh.
  const [ordered, setOrdered] = useState<PresetInfo[] | null>(null);
  const draggingRef = useRef(false);
  useEffect(() => {
    if (!draggingRef.current) setOrdered(null);
  }, [presets]);

  const openSave = useCallback(() => {
    // Prefill with the active user preset's name: saving it again is the
    // one-click "update" path (same name overwrites in place).
    const activeInfo = active ? presets.find((p) => p.id === active.id) : undefined;
    setSaveName(activeInfo && !activeInfo.factory ? activeInfo.name : '');
    setOpen((prev) => (prev === 'save' ? 'none' : 'save'));
  }, [active, presets]);

  const openBrowse = useCallback(() => {
    setSearch('');
    setRenamingId(null);
    setReordering(false);
    setOrdered(null);
    setOpen((prev) => (prev === 'browse' ? 'none' : 'browse'));
  }, []);

  const handleSave = useCallback(async () => {
    const name = saveName.trim();
    if (!name) return;
    const saved = await onSave(name);
    setOpen('none');
    if (saved) toast.show('Preset Saved');
  }, [saveName, onSave, toast]);

  // Close the browser whenever a preset is chosen (list click or ‹ ›), then
  // load. Prev/next wrap at the ends; with no active preset, › starts at the
  // first and ‹ at the last.
  const loadAndClose = useCallback(
    (id: string) => {
      setOpen('none');
      onLoad(id);
    },
    [onLoad]
  );

  const step = useCallback(
    (direction: 1 | -1) => {
      if (presets.length === 0) return;
      const index = active ? presets.findIndex((p) => p.id === active.id) : -1;
      const next =
        index < 0
          ? direction === 1
            ? 0
            : presets.length - 1
          : (index + direction + presets.length) % presets.length;
      loadAndClose(presets[next].id);
    },
    [presets, active, loadAndClose]
  );

  const commitRename = useCallback(() => {
    if (renamingId && renameValue.trim()) onRename(renamingId, renameValue.trim());
    setRenamingId(null);
  }, [renamingId, renameValue, onRename]);

  const filtered = useMemo(() => {
    const list = ordered ?? presets;
    const q = search.trim().toLowerCase();
    return q ? list.filter((p) => p.name.toLowerCase().includes(q)) : list;
  }, [ordered, presets, search]);
  const factoryPresets = filtered.filter((p) => p.factory);
  const userPresets = filtered.filter((p) => !p.factory);

  // PC n loads the nth preset of the full list (display order matches the
  // native list), so the label is the index; built from the optimistic order
  // so numbers track live while dragging. The wire only carries 0-127.
  const pcById = useMemo(() => {
    const map = new Map<string, number>();
    (ordered ?? presets).forEach((preset, i) => {
      if (i <= 127) map.set(preset.id, i);
    });
    return map;
  }, [ordered, presets]);

  const chevronStyle: React.CSSProperties = {
    background: 'transparent',
    border: 'none',
    color: presets.length > 0 ? '#ffffff' : MUTED,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    cursor: presets.length > 0 ? 'pointer' : 'not-allowed',
    padding: '0 4rem',
    alignSelf: 'stretch',
  };

  // Grips only on the full list: a search filter's indices don't match the
  // persisted order, and moves never cross the factory/user boundary.
  const canDrag = reordering && search.trim() === '';

  const handleDragStart = useCallback(() => {
    draggingRef.current = true;
  }, []);

  const handleDragEnd = useCallback(
    (event: DragEndEvent) => {
      draggingRef.current = false;
      const { source } = event.operation;
      if (event.canceled || !isSortable(source)) return;
      if (source.group !== source.initialGroup) return;
      const from = source.initialIndex;
      const to = source.index;
      if (from === to) return;
      const id = String(source.id);
      setOrdered((prev) => {
        const list = prev ?? presets;
        const item = list.find((p) => p.id === id);
        if (!item) return prev;
        const section = list.filter((p) => p.factory === item.factory);
        const others = list.filter((p) => p.factory !== item.factory);
        const moved = arrayMove(section, from, to);
        // User section first, then factory: must match the native list order
        // since the PC labels index straight into this array.
        return item.factory ? [...others, ...moved] : [...moved, ...others];
      });
      onMove(id, to - from);
    },
    [presets, onMove]
  );

  const renderRow = (preset: PresetInfo, sectionIndex: number, group: PresetGroup) => {
    const isRenaming = renamingId === preset.id;
    return (
      <PresetRow
        key={preset.id}
        preset={preset}
        index={sectionIndex}
        group={group}
        sortable={canDrag}
        pcNumber={showPcNumbers ? pcById.get(preset.id) : undefined}
        isActive={active?.id === preset.id}
        isRenaming={isRenaming}
        renameValue={renameValue}
        onRenameChange={setRenameValue}
        onCommitRename={commitRename}
        onCancelRename={() => setRenamingId(null)}
        onLoad={() => loadAndClose(preset.id)}
        onStartRename={() => {
          setRenamingId(preset.id);
          setRenameValue(preset.name);
        }}
        onDelete={() => onDelete(preset.id)}
      />
    );
  };

  return (
    <div
      ref={containerRef}
      style={{ position: 'relative', display: 'flex', alignItems: 'center', gap: '8rem' }}
    >
      {/* ‹ name › pill */}
      <div
        style={{
          display: 'flex',
          alignItems: 'stretch',
          height: '36rem',
          borderRadius: '8rem',
          // Same fill as the model select bar (SEGMENTED_TRACK).
          backgroundColor: SEGMENTED_TRACK,
          padding: '0 4rem',
          flexShrink: 0,
        }}
      >
        <button onClick={() => step(-1)} {...helpProps(HELP.presetPrev)} style={chevronStyle}>
          <ChevronLeft size={14} />
        </button>
        <button
          onClick={openBrowse}
          {...helpProps(HELP.presetBrowse)}
          style={{
            background: 'transparent',
            border: 'none',
            color: active ? '#ffffff' : MUTED,
            fontSize: '14rem',
            fontWeight: 400,
            cursor: 'pointer',
            // Constant width so the pill never resizes with the name; long
            // names ellipsize. Full pill height is the click target.
            width: '150rem',
            height: '100%',
            // Touch: the chevrons' 44 pt hit areas are wider than the chevrons
            // and reach a few points into this button. Raising the name above
            // them keeps the whole name tappable and leaves the chevrons the
            // room *outside* the pill, where nothing else competes.
            ...(IS_COARSE_POINTER ? { position: 'relative' as const, zIndex: 1 } : {}),
            lineHeight: '36rem',
            textAlign: 'center',
            overflow: 'hidden',
            textOverflow: 'ellipsis',
            whiteSpace: 'nowrap',
            padding: '0 6rem',
          }}
        >
          {active?.name ?? 'Presets'}
        </button>
        <button onClick={() => step(1)} {...helpProps(HELP.presetNext)} style={chevronStyle}>
          <ChevronRight size={14} />
        </button>
      </div>

      {/* Save */}
      <button onClick={openSave} {...helpProps(HELP.presetSave)} style={iconButtonStyle}>
        <Save size={18} />
      </button>

      {/* New: back to the factory-default state, greyed once already there. */}
      <IconButton
        onClick={() => {
          setOpen('none');
          onReset();
        }}
        disabled={atDefault}
        help={HELP.presetNew}
        size={28}
      >
        <Plus size={18} />
      </IconButton>

      {/* Save popover */}
      {open === 'save' && (
        <div style={{ ...panelStyle, width: '280rem' }}>
          <div
            style={{ color: '#ffffff', fontSize: '14rem', fontWeight: 600, marginBottom: '12rem' }}
          >
            Save Preset
          </div>
          <input
            className="t3k-touch-field"
            autoFocus
            value={saveName}
            onChange={(e) => setSaveName(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') handleSave();
            }}
            placeholder="Name"
            style={inputStyle}
          />
          <button
            onClick={handleSave}
            disabled={!saveName.trim()}
            style={{
              width: '100%',
              marginTop: '12rem',
              padding: '9rem 0',
              borderRadius: '999rem',
              border: '1rem solid rgba(235, 235, 245, 0.6)',
              background: 'transparent',
              color: saveName.trim() ? '#ffffff' : MUTED,
              fontSize: '13rem',
              cursor: saveName.trim() ? 'pointer' : 'not-allowed',
            }}
          >
            Save
          </button>
        </div>
      )}

      {/* Preset browser. Top/bottom spacing lives in the scroll content so
          the list clips at the search row and the panel border. */}
      {open === 'browse' && (
        <div
          style={{
            ...panelStyle,
            width: '360rem',
            padding: '12rem 12rem 0',
            overflow: 'hidden',
          }}
        >
          <div style={{ display: 'flex', alignItems: 'center', gap: '6rem' }}>
            <div style={{ position: 'relative', flex: 1, minWidth: 0 }}>
              <Search
                size={14}
                color={MUTED}
                style={{
                  position: 'absolute',
                  left: '12rem',
                  top: '50%',
                  transform: 'translateY(-50%)',
                }}
              />
              <input
                className="t3k-touch-field"
                autoFocus
                value={search}
                onChange={(e) => setSearch(e.target.value)}
                placeholder="Search presets"
                style={{ ...inputStyle, padding: '8rem 12rem 8rem 32rem', borderRadius: '10rem' }}
              />
            </div>
            {presets.length > 0 && (
              <button
                onClick={() => setPresetPcNumbersEnabled(!showPcNumbers)}
                aria-pressed={showPcNumbers}
                {...helpProps(HELP.presetPcToggle)}
                style={{
                  ...iconButtonStyle,
                  padding: '7rem',
                  flexShrink: 0,
                  color: showPcNumbers ? '#ffffff' : MUTED,
                  background: showPcNumbers ? 'rgba(255, 255, 255, 0.12)' : 'transparent',
                }}
              >
                <MidiPort size={15} />
              </button>
            )}
            {presets.length > 1 && (
              <button
                onClick={() => setReordering((prev) => !prev)}
                aria-pressed={reordering}
                {...helpProps(HELP.presetReorder)}
                style={{
                  ...iconButtonStyle,
                  padding: '7rem',
                  flexShrink: 0,
                  color: reordering ? '#ffffff' : MUTED,
                  background: reordering ? 'rgba(255, 255, 255, 0.12)' : 'transparent',
                }}
              >
                <ArrowUpDown size={15} />
              </button>
            )}
          </div>

          <DragDropProvider onDragStart={handleDragStart} onDragEnd={handleDragEnd}>
            <div className="hide-scrollbar" style={{ maxHeight: '362rem', overflowY: 'auto' }}>
              <div style={{ padding: '10rem 0 12rem' }}>
                {userPresets.length > 0 && (
                  <>
                    <div style={sectionHeaderStyle}>Your Presets</div>
                    {userPresets.map((preset, i) => renderRow(preset, i, 'user'))}
                  </>
                )}
                {factoryPresets.length > 0 && (
                  <>
                    <div style={sectionHeaderStyle}>TONE3000</div>
                    {factoryPresets.map((preset, i) => renderRow(preset, i, 'factory'))}
                  </>
                )}
                {filtered.length === 0 && (
                  <div
                    style={{
                      color: MUTED,
                      fontSize: '13rem',
                      fontWeight: 400,
                      padding: '12rem 4rem',
                    }}
                  >
                    {presets.length === 0
                      ? 'No presets yet. Save one to get started.'
                      : 'No matches.'}
                  </div>
                )}
              </div>
            </div>
          </DragDropProvider>
        </div>
      )}
    </div>
  );
};
