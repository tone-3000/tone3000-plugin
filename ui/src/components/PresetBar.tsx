import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  ArrowUpDown,
  Check,
  CheckSquare,
  ChevronDown,
  ChevronLeft,
  ChevronRight,
  Copy,
  Folder,
  FolderPlus,
  GripVertical,
  MidiPort,
  Pencil,
  Plus,
  Save,
  Search,
  Square,
  Star,
  Trash2,
  X,
} from './icons';
import { IconButton } from './IconButton';
import { DragDropProvider, useDraggable, useDroppable } from '@dnd-kit/react';
import { PointerActivationConstraints, PointerSensor } from '@dnd-kit/dom';
import type { Sensors } from '@dnd-kit/dom';
import { arrayMove } from '@dnd-kit/helpers';
import type { DragEndEvent } from '@dnd-kit/react';
import type { ActivePreset, PresetInfo } from '../types/chain';
import { useDismissable } from '../hooks/useDismissable';
import { useToast } from './Toast';
import { HELP, helpProps } from './helpText';
import { BRAND_YELLOW, BORDER, FONT_MONO, GRAY, SEGMENTED_TRACK } from './theme';
import { setPresetPcNumbersEnabled, usePresetPcNumbersEnabled } from './uiPreferences';
import { getUiScale } from '../hooks/useUiScale';

const MUTED = GRAY;
const PANEL_BG = '#141416';
const MAX_CATEGORY_LENGTH = 50;

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
  fontWeight: 400,
  padding: '9rem 12rem',
  outline: 'none',
};

const sectionHeaderStyle: React.CSSProperties = {
  color: GRAY,
  fontSize: '14rem',
  fontWeight: 700,
  padding: '8rem 4rem',
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'space-between',
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

interface ConfirmModalProps {
  title: string;
  message: string;
  confirmLabel?: string;
  confirmDestructive?: boolean;
  onConfirm: () => void;
  onCancel: () => void;
}

const ConfirmModal: React.FC<ConfirmModalProps> = ({
  title,
  message,
  confirmLabel = 'Delete',
  confirmDestructive = true,
  onConfirm,
  onCancel,
}) => (
  <div
    style={{
      position: 'absolute',
      inset: 0,
      backgroundColor: 'rgba(0, 0, 0, 0.75)',
      backdropFilter: 'blur(4rem)',
      WebkitBackdropFilter: 'blur(4rem)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      padding: '16rem',
      zIndex: 600,
      boxSizing: 'border-box',
    }}
  >
    <div
      style={{
        backgroundColor: '#1C1C1E',
        border: BORDER,
        borderRadius: '12rem',
        padding: '16rem',
        width: '100%',
        maxWidth: '300rem',
        boxSizing: 'border-box',
        display: 'flex',
        flexDirection: 'column',
        gap: '12rem',
      }}
    >
      <div style={{ color: '#ffffff', fontSize: '14rem', fontWeight: 600 }}>{title}</div>
      <div
        style={{
          color: MUTED,
          fontSize: '12rem',
          fontWeight: 400,
          lineHeight: 1.4,
        }}
      >
        {message}
      </div>
      <div style={{ display: 'flex', justifyContent: 'flex-end', gap: '8rem', marginTop: '4rem' }}>
        <button
          onClick={onCancel}
          style={{
            background: 'transparent',
            border: BORDER,
            borderRadius: '6rem',
            color: '#ffffff',
            padding: '6rem 12rem',
            fontSize: '12rem',
            cursor: 'pointer',
          }}
        >
          Cancel
        </button>
        <button
          onClick={onConfirm}
          style={{
            background: confirmDestructive ? '#d32f2f' : '#2563eb',
            border: 'none',
            borderRadius: '6rem',
            color: '#ffffff',
            padding: '6rem 12rem',
            fontSize: '12rem',
            fontWeight: 600,
            cursor: 'pointer',
          }}
        >
          {confirmLabel}
        </button>
      </div>
    </div>
  </div>
);

interface DroppableCategoryHeaderProps {
  id: string;
  categoryName: string;
  count: number;
  isCollapsed?: boolean;
  onToggleCollapse?: () => void;
  onDeleteCategory?: () => void;
  canDelete?: boolean;
  icon?: React.ReactNode;
}

const DroppableCategoryHeader: React.FC<DroppableCategoryHeaderProps> = ({
  id,
  categoryName,
  count,
  isCollapsed = false,
  onToggleCollapse,
  onDeleteCategory,
  canDelete = true,
  icon,
}) => {
  const { ref, isDropTarget } = useDroppable({
    id: `cat-drop:${id}`,
    type: 'category-target',
    accept: ['user'],
  });

  return (
    <div
      ref={ref}
      onClick={onToggleCollapse}
      {...(onToggleCollapse ? helpProps(HELP.categoryToggle) : {})}
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        padding: '6rem 4rem',
        marginTop: '6rem',
        borderRadius: '6rem',
        backgroundColor: isDropTarget ? 'rgba(255, 255, 255, 0.16)' : 'transparent',
        border: isDropTarget ? '1px dashed rgba(255, 255, 255, 0.45)' : '1px solid transparent',
        transition: 'background-color 0.15s ease, border-color 0.15s ease',
        cursor: onToggleCollapse ? 'pointer' : 'default',
        userSelect: 'none',
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: '6rem' }}>
        {onToggleCollapse && (
          isCollapsed ? (
            <ChevronRight size={13} color={MUTED} style={{ flexShrink: 0 }} />
          ) : (
            <ChevronDown size={13} color={MUTED} style={{ flexShrink: 0 }} />
          )
        )}
        {icon ?? <Folder size={13} color={MUTED} style={{ flexShrink: 0 }} />}
        <span style={{ color: '#ffffff', fontSize: '13rem', fontWeight: 600 }}>{categoryName}</span>
        <span style={{ color: MUTED, fontSize: '11rem', fontWeight: 400 }}>({count})</span>
      </div>
      {canDelete && (
        <button
          onClick={(e) => {
            e.stopPropagation();
            onDeleteCategory?.();
          }}
          {...helpProps(HELP.categoryDelete)}
          aria-label={`Delete category ${categoryName}`}
          style={{ ...iconButtonStyle, padding: '3rem', color: MUTED }}
        >
          <Trash2 size={12} />
        </button>
      )}
    </div>
  );
};

interface EmptyCategoryDropZoneProps {
  category: string;
  label?: string;
}

const EmptyCategoryDropZone: React.FC<EmptyCategoryDropZoneProps> = ({
  category,
  label = 'Empty category (drag presets here)',
}) => {
  const { ref, isDropTarget } = useDroppable({
    id: `cat-empty-drop:${category}`,
    type: 'category-target',
    accept: ['user'],
  });

  return (
    <div
      ref={ref}
      style={{
        color: isDropTarget ? '#ffffff' : MUTED,
        fontSize: '11rem',
        fontStyle: 'italic',
        padding: '8rem 24rem',
        margin: '2rem 4rem',
        borderRadius: '6rem',
        backgroundColor: isDropTarget ? 'rgba(255, 255, 255, 0.14)' : 'rgba(255, 255, 255, 0.02)',
        border: isDropTarget ? '1px dashed rgba(255, 255, 255, 0.5)' : '1px dashed rgba(255, 255, 255, 0.08)',
        transition: 'all 0.15s ease',
      }}
    >
      {label}
    </div>
  );
};

interface PresetRowProps {
  preset: PresetInfo;
  draggable: boolean;
  droppable?: boolean;
  selectable: boolean;
  selected: boolean;
  onToggleSelect: (id: string) => void;
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
  onToggleFavorite: () => void;
}

const PresetRow: React.FC<PresetRowProps> = ({
  preset,
  draggable,
  droppable = true,
  selectable,
  selected,
  onToggleSelect,
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
  onToggleFavorite,
}) => {
  const { ref: dragRef, handleRef, isDragging } = useDraggable({
    id: preset.id,
    type: 'user',
    disabled: !draggable,
  });

  const { ref: dropRef, isDropTarget } = useDroppable({
    id: `preset-drop:${preset.id}`,
    type: 'category-target',
    accept: ['user'],
    disabled: !droppable,
  });

  const setRefs = useCallback(
    (el: HTMLDivElement | null) => {
      dragRef(el);
      dropRef(el);
    },
    [dragRef, dropRef]
  );

  return (
    <div
      ref={setRefs}
      style={{
        display: 'flex',
        alignItems: 'center',
        gap: '6rem',
        height: '32rem',
        padding: '0 4rem',
        opacity: isDragging ? 0.45 : 1,
        borderRadius: '6rem',
        backgroundColor: isDropTarget
          ? 'rgba(255, 255, 255, 0.16)'
          : selected
          ? 'rgba(255, 255, 255, 0.08)'
          : 'transparent',
        border: isDropTarget ? '1px dashed rgba(255, 255, 255, 0.45)' : '1px solid transparent',
        transition: 'background-color 0.15s ease, border-color 0.15s ease',
      }}
    >
      {/* Select checkbox OR active indicator */}
      {selectable ? (
        <button
          onClick={(e) => {
            e.stopPropagation();
            onToggleSelect(preset.id);
          }}
          style={{ ...iconButtonStyle, padding: '2rem', flexShrink: 0 }}
          aria-label={selected ? 'Deselect preset' : 'Select preset'}
          {...helpProps(selected ? HELP.presetDeselect : HELP.presetSelect)}
        >
          {selected ? <CheckSquare size={14} color="#ffffff" /> : <Square size={14} color={MUTED} />}
        </button>
      ) : (
        <span style={{ width: '16rem', flexShrink: 0, display: 'flex', alignItems: 'center' }}>
          {isActive && <Check size={14} color="#ffffff" />}
        </span>
      )}

      {/* Star / Favourite toggle */}
      <button
        onClick={(e) => {
          e.stopPropagation();
          onToggleFavorite();
        }}
        aria-label={preset.favorite ? 'Unstar preset' : 'Star preset'}
        {...helpProps(preset.favorite ? HELP.presetUnfavorite : HELP.presetFavorite)}
        style={{
          ...iconButtonStyle,
          padding: '2rem',
          flexShrink: 0,
          color: preset.favorite ? BRAND_YELLOW : 'rgba(255, 255, 255, 0.25)',
        }}
      >
        <Star size={13} fill={preset.favorite ? BRAND_YELLOW : 'transparent'} />
      </button>

      {/* Name / Inline Rename */}
      {isRenaming ? (
        <input
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
          onClick={() => {
            if (selectable) onToggleSelect(preset.id);
            else onLoad();
          }}
          style={{
            flex: 1,
            minWidth: 0,
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

      {/* PC Number */}
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

      {/* Action buttons and/or Drag handle */}
      {!preset.factory && !isRenaming && (
        <div style={{ display: 'flex', alignItems: 'center', gap: '2rem', flexShrink: 0 }}>
          {!selectable && (
            <>
              <button
                onClick={(e) => {
                  e.stopPropagation();
                  onStartRename();
                }}
                {...helpProps(HELP.presetRename)}
                aria-label="Rename preset"
                style={{ ...iconButtonStyle, padding: '3rem', color: MUTED }}
              >
                <Pencil size={13} />
              </button>
              <button
                onClick={(e) => {
                  e.stopPropagation();
                  onDelete();
                }}
                {...helpProps(HELP.presetDelete)}
                aria-label="Delete preset"
                style={{ ...iconButtonStyle, padding: '3rem', color: MUTED }}
              >
                <Trash2 size={13} />
              </button>
            </>
          )}
          {draggable && (
            <button
              ref={handleRef}
              type="button"
              aria-label="Drag preset"
              {...helpProps(HELP.presetDrag)}
              style={{
                ...iconButtonStyle,
                padding: '3rem',
                flexShrink: 0,
                cursor: isDragging ? 'grabbing' : 'grab',
                touchAction: 'none',
                color: isDragging ? '#ffffff' : MUTED,
              }}
            >
              <GripVertical size={14} />
            </button>
          )}
        </div>
      )}
    </div>
  );
};

interface PresetBarProps {
  active: ActivePreset | null;
  presets: PresetInfo[];
  categories?: string[];
  atDefault: boolean;
  onSave: (name: string) => Promise<{ id: string; name: string } | null>;
  onLoad: (id: string) => void;
  onRename: (id: string, name: string) => void;
  onDelete: (id: string) => void;
  onMove: (id: string, delta: number) => void;
  onReset: () => void;
  onAddCategory?: (name: string) => Promise<boolean | null>;
  onDeleteCategory?: (name: string) => Promise<boolean | null>;
  onSetCategory?: (id: string, category: string) => Promise<boolean | null>;
  onMovePresetsToCategory?: (ids: string[], category: string) => Promise<boolean | null>;
  onSetFavorite?: (id: string, isFavorite: boolean) => Promise<boolean | null>;
  onSetFavorites?: (ids: string[], isFavorite: boolean) => Promise<boolean | null>;
  onDuplicatePresets?: (ids: string[]) => Promise<PresetInfo[] | null>;
  onDeletePresets?: (ids: string[]) => Promise<boolean | null>;
}

type OpenPanel = 'none' | 'save' | 'browse';

export const PresetBar: React.FC<PresetBarProps> = ({
  active,
  presets,
  categories = [],
  atDefault,
  onSave,
  onLoad,
  onRename,
  onDelete,
  onMove,
  onReset,
  onAddCategory,
  onDeleteCategory,
  onMovePresetsToCategory,
  onSetFavorite,
  onSetFavorites,
  onDuplicatePresets,
  onDeletePresets,
}) => {
  const [open, setOpen] = useState<OpenPanel>('none');
  const [saveName, setSaveName] = useState('');
  const [search, setSearch] = useState('');
  const [renamingId, setRenamingId] = useState<string | null>(null);
  const [renameValue, setRenameValue] = useState('');

  // Mode toggles
  const [reordering, setReordering] = useState(false);
  const [multiSelect, setMultiSelect] = useState(false);
  const [selectedIds, setSelectedIds] = useState<Set<string>>(new Set());

  // Category creation & deletion state
  const [isCreatingCategory, setIsCreatingCategory] = useState(false);
  const [newCategoryName, setNewCategoryName] = useState('');
  const [categoryToDelete, setCategoryToDelete] = useState<string | null>(null);

  // Bulk delete confirmation modal state
  const [isConfirmingBulkDelete, setIsConfirmingBulkDelete] = useState(false);

  // Move-to popover menu
  const [isMoveMenuOpen, setIsMoveMenuOpen] = useState(false);

  // Collapsed categories state (keys: '__favourites', '__uncategorized', or user category names)
  const [collapsedCategories, setCollapsedCategories] = useState<Set<string>>(new Set());

  const toggleCollapseCategory = useCallback((catKey: string) => {
    setCollapsedCategories((prev) => {
      const next = new Set(prev);
      if (next.has(catKey)) next.delete(catKey);
      else next.add(catKey);
      return next;
    });
  }, []);

  const sensors: Sensors = useMemo(
    () => [
      PointerSensor.configure({
        activationConstraints: () => [
          new PointerActivationConstraints.Distance({
            value: 4 * getUiScale(),
          }),
        ],
      }),
    ],
    []
  );

  const showPcNumbers = usePresetPcNumbersEnabled();
  const toast = useToast();
  const containerRef = useRef<HTMLDivElement | null>(null);
  const closePanels = useCallback(() => {
    setOpen('none');
    setMultiSelect(false);
    setSelectedIds(new Set());
    setIsCreatingCategory(false);
    setCategoryToDelete(null);
    setIsConfirmingBulkDelete(false);
    setIsMoveMenuOpen(false);
  }, []);
  useDismissable(open !== 'none', containerRef, closePanels);

  // Optimistic order while a drag is in flight
  const [ordered, setOrdered] = useState<PresetInfo[] | null>(null);
  const draggingRef = useRef(false);
  useEffect(() => {
    if (!draggingRef.current) setOrdered(null);
  }, [presets]);

  const openSave = useCallback(() => {
    const activeInfo = active ? presets.find((p) => p.id === active.id) : undefined;
    setSaveName(activeInfo && !activeInfo.factory ? activeInfo.name : '');
    setOpen((prev) => (prev === 'save' ? 'none' : 'save'));
  }, [active, presets]);

  const openBrowse = useCallback(() => {
    setSearch('');
    setRenamingId(null);
    setReordering(false);
    setMultiSelect(false);
    setSelectedIds(new Set());
    setIsCreatingCategory(false);
    setCategoryToDelete(null);
    setIsConfirmingBulkDelete(false);
    setIsMoveMenuOpen(false);
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

  // Category creation
  const handleCreateCategory = useCallback(async () => {
    const trimmed = newCategoryName.trim();
    if (!trimmed) {
      setIsCreatingCategory(false);
      return;
    }
    if (trimmed.length > MAX_CATEGORY_LENGTH) {
      toast.show(`Category name must be ${MAX_CATEGORY_LENGTH} characters or less`);
      return;
    }
    const exists = categories.some((c) => c.localeCompare(trimmed, undefined, { sensitivity: 'base' }) === 0);
    if (exists) {
      toast.show(`Category "${trimmed}" already exists`);
      return;
    }
    const ok = await onAddCategory?.(trimmed);
    if (ok) {
      toast.show(`Category "${trimmed}" created`);
      setNewCategoryName('');
      setIsCreatingCategory(false);
    }
  }, [newCategoryName, categories, onAddCategory, toast]);

  // Category deletion
  const handleConfirmDeleteCategory = useCallback(async () => {
    if (!categoryToDelete) return;
    const cat = categoryToDelete;
    setCategoryToDelete(null);
    const ok = await onDeleteCategory?.(cat);
    if (ok) toast.show(`Category "${cat}" deleted`);
  }, [categoryToDelete, onDeleteCategory, toast]);

  // Multi-select actions
  const toggleSelectPreset = useCallback((id: string) => {
    setSelectedIds((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  }, []);

  const handleDuplicateSelected = useCallback(async () => {
    if (selectedIds.size === 0) return;
    const ids = Array.from(selectedIds);
    const dupes = await onDuplicatePresets?.(ids);
    if (dupes && dupes.length > 0) {
      toast.show(dupes.length === 1 ? 'Preset duplicated' : `Duplicated ${dupes.length} presets`);
      setSelectedIds(new Set());
    }
  }, [selectedIds, onDuplicatePresets, toast]);

  const handleConfirmBulkDelete = useCallback(async () => {
    setIsConfirmingBulkDelete(false);
    if (selectedIds.size === 0) return;
    const ids = Array.from(selectedIds);
    const ok = await onDeletePresets?.(ids);
    if (ok) {
      toast.show(ids.length === 1 ? 'Preset deleted' : `Deleted ${ids.length} presets`);
      setSelectedIds(new Set());
    }
  }, [selectedIds, onDeletePresets, toast]);

  const handleMoveSelectedTo = useCallback(
    async (targetCategory: string) => {
      setIsMoveMenuOpen(false);
      if (selectedIds.size === 0) return;
      const ids = Array.from(selectedIds);
      const ok = await onMovePresetsToCategory?.(ids, targetCategory);
      if (ok) {
        const dest = targetCategory || 'Your Presets';
        toast.show(
          ids.length === 1
            ? `Moved to ${dest}`
            : `Moved ${ids.length} presets to ${dest}`
        );
        setSelectedIds(new Set());
      }
    },
    [selectedIds, onMovePresetsToCategory, toast]
  );

  // Filtered preset list
  const filtered = useMemo(() => {
    const list = ordered ?? presets;
    const q = search.trim().toLowerCase();
    return q ? list.filter((p) => p.name.toLowerCase().includes(q)) : list;
  }, [ordered, presets, search]);

  const factoryPresets = useMemo(() => filtered.filter((p) => p.factory), [filtered]);
  const allUserPresets = useMemo(() => filtered.filter((p) => !p.factory), [filtered]);

  // Favourites pinned group (any starred presets)
  const favouritePresets = useMemo(
    () => filtered.filter((p) => p.favorite),
    [filtered]
  );

  // Non-favourited user presets (sit in their respective categories or root)
  const nonFavouriteUserPresets = useMemo(
    () => allUserPresets.filter((p) => !p.favorite),
    [allUserPresets]
  );

  // Root user presets (no category assigned)
  const rootUserPresets = useMemo(
    () => nonFavouriteUserPresets.filter((p) => !p.category || p.category.trim() === ''),
    [nonFavouriteUserPresets]
  );

  // Sorted user categories
  const sortedCategories = useMemo(() => {
    const cats = [...categories];
    cats.sort((a, b) => a.localeCompare(b, undefined, { sensitivity: 'base' }));
    return cats;
  }, [categories]);

  // Map category to its non-favourite presets
  const presetsByCategory = useMemo(() => {
    const map = new Map<string, PresetInfo[]>();
    for (const cat of sortedCategories) {
      map.set(
        cat,
        nonFavouriteUserPresets.filter(
          (p) => p.category && p.category.localeCompare(cat, undefined, { sensitivity: 'base' }) === 0
        )
      );
    }
    return map;
  }, [sortedCategories, nonFavouriteUserPresets]);

  // PC number mapping for full list
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

  const canDrag = search.trim() === '';

  const handleDragStart = useCallback(() => {
    draggingRef.current = true;
  }, []);

  const handleDragEnd = useCallback(
    (event: DragEndEvent) => {
      draggingRef.current = false;
      const { source, target } = event.operation;
      if (event.canceled || !source || !target) return;

      const sourceId = String(source.id);
      const targetId = String(target.id);
      const sourcePreset = presets.find((p) => p.id === sourceId);

      const targetPresetId = targetId.startsWith('preset-drop:')
        ? targetId.slice('preset-drop:'.length)
        : targetId;
      const targetPreset = presets.find((p) => p.id === targetPresetId);

      const ids = selectedIds.has(sourceId) ? Array.from(selectedIds) : [sourceId];

      const isFromFavorites = sourcePreset?.favorite === true;
      const isTargetInFavorites =
        targetId === 'cat-drop:__favourites' ||
        targetId === 'cat-empty-drop:__favourites' ||
        (targetPreset != null && targetPreset.favorite === true);

      // CASE A: Dragged INTO Favourites (from outside Favourites)
      if (isTargetInFavorites && !isFromFavorites) {
        if (onSetFavorites) {
          onSetFavorites(ids, true);
        } else {
          for (const id of ids) onSetFavorite?.(id, true);
        }
        setSelectedIds(new Set());
        toast.show(ids.length > 1 ? `Starred & added ${ids.length} presets to Favourites` : 'Starred & added to Favourites');
        return;
      }

      // CASE B: Dragged OUT OF Favourites (into uncategorized or a category)
      if (isFromFavorites && !isTargetInFavorites) {
        // Automatically remove the star
        if (onSetFavorites) {
          onSetFavorites(ids, false);
        } else {
          for (const id of ids) onSetFavorite?.(id, false);
        }

        // Determine destination category
        let destCat = '';
        if (targetId.startsWith('cat-drop:')) {
          destCat = targetId.slice('cat-drop:'.length);
        } else if (targetId.startsWith('cat-empty-drop:')) {
          destCat = targetId.slice('cat-empty-drop:'.length);
        } else if (targetPreset && !targetPreset.factory) {
          destCat = targetPreset.category ?? '';
        }

        if (destCat !== undefined) {
          onMovePresetsToCategory?.(ids, destCat);
        }

        setSelectedIds(new Set());
        const destName = destCat || 'All Uncategorized';
        toast.show(
          ids.length > 1
            ? `Unstarred & moved ${ids.length} presets to ${destName}`
            : `Unstarred & moved to ${destName}`
        );
        return;
      }

      // CASE C: Standard move to a category drop header
      if (targetId.startsWith('cat-drop:')) {
        const destCat = targetId.slice('cat-drop:'.length);
        if (destCat !== '__favourites') {
          onMovePresetsToCategory?.(ids, destCat);
          const dest = destCat || 'All Uncategorized';
          toast.show(ids.length > 1 ? `Moved ${ids.length} presets to ${dest}` : `Moved to ${dest}`);
          setSelectedIds(new Set());
          return;
        }
      }

      // CASE D: Standard move to an empty category drop zone
      if (targetId.startsWith('cat-empty-drop:')) {
        const destCat = targetId.slice('cat-empty-drop:'.length);
        if (destCat !== '__favourites') {
          onMovePresetsToCategory?.(ids, destCat);
          const dest = destCat || 'All Uncategorized';
          toast.show(ids.length > 1 ? `Moved ${ids.length} presets to ${dest}` : `Moved to ${dest}`);
          setSelectedIds(new Set());
          return;
        }
      }

      // CASE E: Dropped onto another preset row
      if (targetPreset && sourcePreset) {
        if (reordering) {
          const from = presets.findIndex((p) => p.id === sourceId);
          const to = presets.findIndex((p) => p.id === targetPreset.id);
          if (from !== -1 && to !== -1 && from !== to) {
            setOrdered((prev) => {
              const list = prev ?? presets;
              const item = list.find((p) => p.id === sourceId);
              if (!item) return prev;
              const section = list.filter((p) => p.factory === item.factory);
              const others = list.filter((p) => p.factory !== item.factory);
              const sectionFrom = section.findIndex((p) => p.id === sourceId);
              const sectionTo = section.findIndex((p) => p.id === targetPreset.id);
              if (sectionFrom === -1 || sectionTo === -1) return prev;
              const moved = arrayMove(section, sectionFrom, sectionTo);
              return item.factory ? [...others, ...moved] : [...moved, ...others];
            });
            onMove(sourceId, to - from);
          }
          return;
        }

        if (!sourcePreset.factory && !targetPreset.factory) {
          const sourceCat = sourcePreset.category ?? '';
          const targetCat = targetPreset.category ?? '';
          if (sourceCat.localeCompare(targetCat, undefined, { sensitivity: 'base' }) !== 0) {
            onMovePresetsToCategory?.(ids, targetCat);
            const dest = targetCat || 'All Uncategorized';
            toast.show(ids.length > 1 ? `Moved ${ids.length} presets to ${dest}` : `Moved to ${dest}`);
            setSelectedIds(new Set());
            return;
          }
        }
      }
    },
    [presets, selectedIds, reordering, onMove, onMovePresetsToCategory, onSetFavorite, onSetFavorites, toast]
  );

  const renderRow = (preset: PresetInfo) => {
    const isRenaming = renamingId === preset.id;
    const isDraggable = !preset.factory ? canDrag : reordering && canDrag;
    return (
      <PresetRow
        key={preset.id}
        preset={preset}
        draggable={isDraggable}
        droppable={!preset.factory || reordering}
        selectable={multiSelect && !preset.factory}
        selected={selectedIds.has(preset.id)}
        onToggleSelect={toggleSelectPreset}
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
        onToggleFavorite={() => onSetFavorite?.(preset.id, !preset.favorite)}
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
            width: '150rem',
            height: '100%',
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

      {/* Save button */}
      <button onClick={openSave} {...helpProps(HELP.presetSave)} style={iconButtonStyle}>
        <Save size={18} />
      </button>

      {/* New button: back to default */}
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

      {/* Preset Browser */}
      {open === 'browse' && (
        <div
          style={{
            ...panelStyle,
            width: '380rem',
            padding: '12rem 12rem 0',
            overflow: 'hidden',
          }}
        >
          {/* Top toolbar */}
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
                autoFocus
                value={search}
                onChange={(e) => setSearch(e.target.value)}
                placeholder="Search presets"
                style={{ ...inputStyle, padding: '8rem 12rem 8rem 32rem', borderRadius: '10rem' }}
              />
            </div>

            {/* PC Numbers toggle */}
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

            {/* Reorder toggle */}
            {presets.length > 1 && !multiSelect && (
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

            {/* Multi-select toggle */}
            {allUserPresets.length > 0 && !reordering && (
              <button
                onClick={() => {
                  setMultiSelect((prev) => {
                    if (prev) setSelectedIds(new Set());
                    return !prev;
                  });
                }}
                aria-pressed={multiSelect}
                aria-label="Toggle bulk selection mode"
                {...helpProps(HELP.presetMultiSelectToggle)}
                style={{
                  ...iconButtonStyle,
                  padding: '7rem',
                  flexShrink: 0,
                  color: multiSelect ? '#ffffff' : MUTED,
                  background: multiSelect ? 'rgba(255, 255, 255, 0.12)' : 'transparent',
                }}
              >
                <CheckSquare size={15} />
              </button>
            )}
          </div>

          {/* Bulk actions bar */}
          {multiSelect && selectedIds.size > 0 && (
            <div
              style={{
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'space-between',
                padding: '8rem 10rem',
                marginTop: '8rem',
                backgroundColor: 'rgba(255, 255, 255, 0.08)',
                borderRadius: '8rem',
                border: BORDER,
                position: 'relative',
              }}
            >
              <span style={{ fontSize: '12rem', color: '#ffffff', fontWeight: 600 }}>
                {selectedIds.size} selected
              </span>

              <div style={{ display: 'flex', alignItems: 'center', gap: '6rem' }}>
                {/* Move to dropdown */}
                <div style={{ position: 'relative' }}>
                  <button
                    onClick={() => setIsMoveMenuOpen((prev) => !prev)}
                    {...helpProps(HELP.bulkMove)}
                    style={{
                      ...iconButtonStyle,
                      padding: '4rem 8rem',
                      borderRadius: '6rem',
                      border: BORDER,
                      fontSize: '11rem',
                      gap: '4rem',
                    }}
                  >
                    <Folder size={12} />
                    Move to
                  </button>
                  {isMoveMenuOpen && (
                    <div
                      style={{
                        position: 'absolute',
                        top: 'calc(100% + 4rem)',
                        right: 0,
                        backgroundColor: '#1C1C1E',
                        border: BORDER,
                        borderRadius: '8rem',
                        padding: '4rem',
                        minWidth: '150rem',
                        zIndex: 700,
                        display: 'flex',
                        flexDirection: 'column',
                        gap: '2rem',
                        boxShadow: '0 8rem 16rem rgba(0,0,0,0.5)',
                      }}
                    >
                      <button
                        onClick={() => handleMoveSelectedTo('')}
                        style={{
                          background: 'transparent',
                          border: 'none',
                          color: '#ffffff',
                          textAlign: 'left',
                          fontSize: '12rem',
                          padding: '6rem 8rem',
                          borderRadius: '4rem',
                          cursor: 'pointer',
                        }}
                      >
                        Your Presets (root)
                      </button>
                      {sortedCategories.map((cat) => (
                        <button
                          key={cat}
                          onClick={() => handleMoveSelectedTo(cat)}
                          style={{
                            background: 'transparent',
                            border: 'none',
                            color: '#ffffff',
                            textAlign: 'left',
                            fontSize: '12rem',
                            padding: '6rem 8rem',
                            borderRadius: '4rem',
                            cursor: 'pointer',
                            overflow: 'hidden',
                            textOverflow: 'ellipsis',
                            whiteSpace: 'nowrap',
                          }}
                        >
                          {cat}
                        </button>
                      ))}
                    </div>
                  )}
                </div>

                {/* Duplicate selected */}
                <button
                  onClick={handleDuplicateSelected}
                  aria-label="Duplicate selected presets"
                  {...helpProps(HELP.bulkDuplicate)}
                  style={{
                    ...iconButtonStyle,
                    padding: '4rem 8rem',
                    borderRadius: '6rem',
                    border: BORDER,
                    fontSize: '11rem',
                    gap: '4rem',
                  }}
                >
                  <Copy size={12} />
                  Duplicate
                </button>

                {/* Delete selected */}
                <button
                  onClick={() => setIsConfirmingBulkDelete(true)}
                  aria-label="Delete selected presets"
                  {...helpProps(HELP.bulkDelete)}
                  style={{
                    ...iconButtonStyle,
                    padding: '4rem 8rem',
                    borderRadius: '6rem',
                    border: BORDER,
                    fontSize: '11rem',
                    color: '#ff6b6b',
                    gap: '4rem',
                  }}
                >
                  <Trash2 size={12} />
                  Delete
                </button>

                {/* Deselect all */}
                <button
                  onClick={() => setSelectedIds(new Set())}
                  aria-label="Deselect all"
                  {...helpProps(HELP.presetDeselectAll)}
                  style={{ ...iconButtonStyle, padding: '4rem' }}
                >
                  <X size={13} />
                </button>
              </div>
            </div>
          )}

          {/* Preset list content */}
          <DragDropProvider sensors={sensors} onDragStart={handleDragStart} onDragEnd={handleDragEnd}>
            <div className="preset-scrollbar" style={{ maxHeight: '380rem', overflowY: 'auto', paddingRight: '4rem' }}>
              <div style={{ padding: '10rem 0 12rem' }}>
                {/* 1. Pinned Favourites Category */}
                <div style={{ marginBottom: '10rem' }}>
                  <DroppableCategoryHeader
                    id="__favourites"
                    categoryName="Favourites"
                    count={favouritePresets.length}
                    isCollapsed={collapsedCategories.has('__favourites')}
                    onToggleCollapse={() => toggleCollapseCategory('__favourites')}
                    canDelete={false}
                    icon={<Star size={13} fill={BRAND_YELLOW} color={BRAND_YELLOW} style={{ flexShrink: 0 }} />}
                  />
                  {!collapsedCategories.has('__favourites') && (
                    favouritePresets.length > 0 ? (
                      favouritePresets.map((preset) => renderRow(preset))
                    ) : (
                      <EmptyCategoryDropZone
                        category="__favourites"
                        label="Empty favourites (drag presets here to star)"
                      />
                    )
                  )}
                </div>

                {/* 2. Your Presets Section */}
                {(allUserPresets.length > 0 || categories.length > 0) && (
                  <>
                    <div style={sectionHeaderStyle}>
                      <span>Your Presets</span>
                      <button
                        onClick={() => setIsCreatingCategory((prev) => !prev)}
                        aria-label="Create category"
                        {...helpProps(HELP.categoryCreate)}
                        style={{
                          ...iconButtonStyle,
                          padding: '3rem',
                          color: isCreatingCategory ? '#ffffff' : MUTED,
                        }}
                      >
                        <FolderPlus size={15} />
                      </button>
                    </div>

                    {/* Inline Create Category Input */}
                    {isCreatingCategory && (
                      <div
                        style={{
                          display: 'flex',
                          alignItems: 'center',
                          gap: '6rem',
                          padding: '6rem 4rem',
                          marginBottom: '6rem',
                        }}
                      >
                        <Folder size={14} color={MUTED} style={{ flexShrink: 0 }} />
                        <input
                          autoFocus
                          value={newCategoryName}
                          maxLength={MAX_CATEGORY_LENGTH}
                          onChange={(e) => setNewCategoryName(e.target.value)}
                          onKeyDown={(e) => {
                            if (e.key === 'Enter') handleCreateCategory();
                            if (e.key === 'Escape') setIsCreatingCategory(false);
                          }}
                          placeholder="New category..."
                          style={{
                            ...inputStyle,
                            padding: '4rem 8rem',
                            borderRadius: '6rem',
                            flex: 1,
                            fontSize: '12rem',
                          }}
                        />
                        <button
                          onClick={handleCreateCategory}
                          style={{ ...iconButtonStyle, padding: '3rem' }}
                        >
                          <Check size={14} />
                        </button>
                        <button
                          onClick={() => {
                            setIsCreatingCategory(false);
                            setNewCategoryName('');
                          }}
                          style={{ ...iconButtonStyle, padding: '3rem' }}
                        >
                          <X size={14} />
                        </button>
                      </div>
                    )}

                    {/* Droppable Root Header (moves presets to root) */}
                    <DroppableCategoryHeader
                      id=""
                      categoryName="All Uncategorized"
                      count={rootUserPresets.length}
                      isCollapsed={collapsedCategories.has('__uncategorized')}
                      onToggleCollapse={() => toggleCollapseCategory('__uncategorized')}
                      onDeleteCategory={() => {}}
                      canDelete={false}
                    />

                    {/* Root Presets (non-favourited) */}
                    {!collapsedCategories.has('__uncategorized') && (
                      <>
                        {rootUserPresets.map((preset) => renderRow(preset))}
                        {rootUserPresets.length === 0 && categories.length > 0 && (
                          <EmptyCategoryDropZone
                            category=""
                            label="No uncategorized presets (drag here to uncategorize)"
                          />
                        )}
                      </>
                    )}

                    {/* User Categories */}
                    {sortedCategories.map((category) => {
                      const categoryPresets = presetsByCategory.get(category) ?? [];
                      const isCollapsed = collapsedCategories.has(category);
                      return (
                        <div key={category} style={{ marginTop: '6rem' }}>
                          <DroppableCategoryHeader
                            id={category}
                            categoryName={category}
                            count={categoryPresets.length}
                            isCollapsed={isCollapsed}
                            onToggleCollapse={() => toggleCollapseCategory(category)}
                            onDeleteCategory={() => setCategoryToDelete(category)}
                            canDelete={true}
                          />
                          {!isCollapsed && (
                            categoryPresets.length > 0 ? (
                              categoryPresets.map((preset) => renderRow(preset))
                            ) : (
                              <EmptyCategoryDropZone category={category} />
                            )
                          )}
                        </div>
                      );
                    })}
                  </>
                )}

                {/* 3. TONE3000 Factory Section */}
                {factoryPresets.length > 0 && (
                  <>
                    <div
                      onClick={() => toggleCollapseCategory('__factory')}
                      {...helpProps(HELP.categoryToggle)}
                      style={{
                        ...sectionHeaderStyle,
                        marginTop: '12rem',
                        cursor: 'pointer',
                        userSelect: 'none',
                        display: 'flex',
                        alignItems: 'center',
                        gap: '6rem',
                      }}
                    >
                      {collapsedCategories.has('__factory') ? (
                        <ChevronRight size={13} color={MUTED} style={{ flexShrink: 0 }} />
                      ) : (
                        <ChevronDown size={13} color={MUTED} style={{ flexShrink: 0 }} />
                      )}
                      <span>TONE3000</span>
                      <span style={{ color: MUTED, fontSize: '11rem', fontWeight: 400 }}>
                        ({factoryPresets.length})
                      </span>
                    </div>
                    {!collapsedCategories.has('__factory') &&
                      factoryPresets.map((preset) => renderRow(preset))}
                  </>
                )}

                {/* Empty State */}
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

          {/* Confirm Delete Category Modal */}
          {categoryToDelete && (
            <ConfirmModal
              title="Delete Category"
              message={`Are you sure you want to delete "${categoryToDelete}"? Presets inside will be moved to Your Presets.`}
              confirmLabel="Delete"
              onConfirm={handleConfirmDeleteCategory}
              onCancel={() => setCategoryToDelete(null)}
            />
          )}

          {/* Confirm Bulk Delete Presets Modal */}
          {isConfirmingBulkDelete && (
            <ConfirmModal
              title="Delete Presets"
              message={`Are you sure you want to delete ${selectedIds.size} selected preset(s)? This cannot be undone.`}
              confirmLabel="Delete"
              onConfirm={handleConfirmBulkDelete}
              onCancel={() => setIsConfirmingBulkDelete(false)}
            />
          )}
        </div>
      )}
    </div>
  );
};
