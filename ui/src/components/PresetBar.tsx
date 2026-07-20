import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Check, ChevronLeft, ChevronRight, Pencil, Save, Search, Trash2 } from 'lucide-react';
import type { ActivePreset, PresetInfo } from '../types/chain';
import { HELP, helpProps } from './helpText';
import { BORDER, GRAY } from './theme';

/**
 * Top-bar preset controls: ‹ name › pill + save button, with two anchored
 * panels — the save popover (name + save) and the preset browser (search,
 * TONE3000 factory section, user section with inline rename/delete).
 *
 * Pure view: the list and all mutations come from usePresets, the active
 * preset rides the chain state poll. Prev/next walk the list in its native
 * order (factory first, then user, each sorted by name), wrapping.
 */

const MUTED = GRAY;
const PANEL_BG = '#141416';

const panelStyle: React.CSSProperties = {
  position: 'absolute',
  top: 'calc(100% + 10px)',
  left: '-8px',
  backgroundColor: PANEL_BG,
  border: BORDER,
  borderRadius: '14px',
  padding: '16px',
  zIndex: 200,
  boxShadow: '0 12px 32px rgba(0, 0, 0, 0.55)',
  boxSizing: 'border-box',
};

const inputStyle: React.CSSProperties = {
  width: '100%',
  boxSizing: 'border-box',
  backgroundColor: '#1C1C1E',
  border: BORDER,
  borderRadius: '10px',
  color: '#ffffff',
  fontSize: '13px',
  // Typed text and placeholders are body text: reset the global 600 default.
  fontWeight: 400,
  padding: '9px 12px',
  outline: 'none',
};

const iconButtonStyle: React.CSSProperties = {
  background: 'transparent',
  border: 'none',
  color: '#ffffff',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  cursor: 'pointer',
  borderRadius: '4px',
  padding: '5px',
};

interface PresetBarProps {
  active: ActivePreset | null;
  presets: PresetInfo[];
  onSave: (name: string) => Promise<{ id: string; name: string } | null>;
  onLoad: (id: string) => void;
  onRename: (id: string, name: string) => void;
  onDelete: (id: string) => void;
}

type OpenPanel = 'none' | 'save' | 'browse';

export const PresetBar: React.FC<PresetBarProps> = ({
  active,
  presets,
  onSave,
  onLoad,
  onRename,
  onDelete,
}) => {
  const [open, setOpen] = useState<OpenPanel>('none');
  const [saveName, setSaveName] = useState('');
  const [search, setSearch] = useState('');
  const [renamingId, setRenamingId] = useState<string | null>(null);
  const [renameValue, setRenameValue] = useState('');
  const containerRef = useRef<HTMLDivElement | null>(null);

  // Close panels on outside click or Escape.
  useEffect(() => {
    if (open === 'none') return;
    const onPointerDown = (e: PointerEvent) => {
      if (!containerRef.current?.contains(e.target as Node)) setOpen('none');
    };
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setOpen('none');
    };
    document.addEventListener('pointerdown', onPointerDown);
    document.addEventListener('keydown', onKeyDown);
    return () => {
      document.removeEventListener('pointerdown', onPointerDown);
      document.removeEventListener('keydown', onKeyDown);
    };
  }, [open]);

  const openSave = useCallback(() => {
    // Prefill with the active user preset's name — saving it again is the
    // one-click "update" path (same name overwrites in place).
    const activeInfo = active ? presets.find((p) => p.id === active.id) : undefined;
    setSaveName(activeInfo && !activeInfo.factory ? activeInfo.name : '');
    setOpen((prev) => (prev === 'save' ? 'none' : 'save'));
  }, [active, presets]);

  const openBrowse = useCallback(() => {
    setSearch('');
    setRenamingId(null);
    setOpen((prev) => (prev === 'browse' ? 'none' : 'browse'));
  }, []);

  const handleSave = useCallback(async () => {
    const name = saveName.trim();
    if (!name) return;
    await onSave(name);
    setOpen('none');
  }, [saveName, onSave]);

  // Prev/next step through the list in order, wrapping at the ends. With no
  // active preset, › starts at the first and ‹ at the last.
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
      onLoad(presets[next].id);
    },
    [presets, active, onLoad]
  );

  const commitRename = useCallback(() => {
    if (renamingId && renameValue.trim()) onRename(renamingId, renameValue.trim());
    setRenamingId(null);
  }, [renamingId, renameValue, onRename]);

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    return q ? presets.filter((p) => p.name.toLowerCase().includes(q)) : presets;
  }, [presets, search]);
  const factoryPresets = filtered.filter((p) => p.factory);
  const userPresets = filtered.filter((p) => !p.factory);

  const chevronStyle: React.CSSProperties = {
    background: 'transparent',
    border: 'none',
    color: presets.length > 0 ? '#ffffff' : MUTED,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    cursor: presets.length > 0 ? 'pointer' : 'default',
    padding: '4px',
  };

  const renderRow = (preset: PresetInfo) => {
    const isActive = active?.id === preset.id;
    const isRenaming = renamingId === preset.id;
    return (
      <div
        key={preset.id}
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: '8px',
          height: '32px',
          padding: '0 4px',
        }}
      >
        <span style={{ width: '16px', flexShrink: 0, display: 'flex', alignItems: 'center' }}>
          {isActive && <Check size={14} color="#ffffff" />}
        </span>
        {isRenaming ? (
          <input
            autoFocus
            value={renameValue}
            onChange={(e) => setRenameValue(e.target.value)}
            onBlur={commitRename}
            onKeyDown={(e) => {
              if (e.key === 'Enter') commitRename();
              if (e.key === 'Escape') setRenamingId(null);
            }}
            style={{ ...inputStyle, padding: '4px 8px', borderRadius: '6px', flex: 1 }}
          />
        ) : (
          <button
            onClick={() => onLoad(preset.id)}
            style={{
              flex: 1,
              background: 'transparent',
              border: 'none',
              textAlign: 'left',
              color: isActive ? '#ffffff' : MUTED,
              fontSize: '13px',
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
        {!preset.factory && !isRenaming && (
          <>
            <button
              onClick={() => {
                setRenamingId(preset.id);
                setRenameValue(preset.name);
              }}
              {...helpProps(HELP.presetRename)}
              style={{ ...iconButtonStyle, padding: '3px' }}
            >
              <Pencil size={13} />
            </button>
            <button
              onClick={() => onDelete(preset.id)}
              {...helpProps(HELP.presetDelete)}
              style={{ ...iconButtonStyle, padding: '3px' }}
            >
              <Trash2 size={13} />
            </button>
          </>
        )}
      </div>
    );
  };

  return (
    <div
      ref={containerRef}
      style={{ position: 'relative', display: 'flex', alignItems: 'center', gap: '8px' }}
    >
      {/* ‹ name › pill */}
      <div
        style={{
          display: 'flex',
          alignItems: 'center',
          height: '28px',
          borderRadius: '14px',
          backgroundColor: '#1C1C1E',
          padding: '0 4px',
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
            fontSize: '12px',
            fontWeight: 400,
            cursor: 'pointer',
            // Constant width so the pill never resizes with the name; long
            // names ellipsize.
            width: '150px',
            textAlign: 'center',
            overflow: 'hidden',
            textOverflow: 'ellipsis',
            whiteSpace: 'nowrap',
            padding: '0 6px',
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

      {/* Save popover */}
      {open === 'save' && (
        <div style={{ ...panelStyle, width: '280px' }}>
          <div
            style={{ color: '#ffffff', fontSize: '14px', fontWeight: 600, marginBottom: '12px' }}
          >
            Save Preset
          </div>
          <input
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
              marginTop: '12px',
              padding: '9px 0',
              borderRadius: '999px',
              border: '1px solid rgba(235, 235, 245, 0.6)',
              background: 'transparent',
              color: saveName.trim() ? '#ffffff' : MUTED,
              fontSize: '13px',
              cursor: saveName.trim() ? 'pointer' : 'default',
            }}
          >
            Save
          </button>
        </div>
      )}

      {/* Preset browser */}
      {open === 'browse' && (
        <div style={{ ...panelStyle, width: '300px', padding: '12px' }}>
          <div style={{ position: 'relative', marginBottom: '10px' }}>
            <Search
              size={14}
              color={MUTED}
              style={{
                position: 'absolute',
                left: '12px',
                top: '50%',
                transform: 'translateY(-50%)',
              }}
            />
            <input
              autoFocus
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              placeholder="Search presets"
              style={{ ...inputStyle, padding: '8px 12px 8px 32px', borderRadius: '10px' }}
            />
          </div>

          <div className="hide-scrollbar" style={{ maxHeight: '340px', overflowY: 'auto' }}>
            {factoryPresets.length > 0 && (
              <>
                <div
                  style={{
                    color: '#ffffff',
                    fontSize: '13px',
                    fontWeight: 600,
                    padding: '8px 4px',
                  }}
                >
                  TONE3000
                </div>
                {factoryPresets.map(renderRow)}
              </>
            )}
            {userPresets.length > 0 && (
              <>
                <div
                  style={{
                    color: '#ffffff',
                    fontSize: '13px',
                    fontWeight: 600,
                    padding: '8px 4px',
                  }}
                >
                  Your Presets
                </div>
                {userPresets.map(renderRow)}
              </>
            )}
            {filtered.length === 0 && (
              <div style={{ color: MUTED, fontSize: '13px', fontWeight: 400, padding: '12px 4px' }}>
                {presets.length === 0 ? 'No presets yet — save one to get started.' : 'No matches.'}
              </div>
            )}
          </div>
        </div>
      )}
    </div>
  );
};
