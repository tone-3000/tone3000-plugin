import React, { useState, useRef, useCallback } from 'react';
import { ChevronLeft, ChevronRight, FolderClosed } from 'lucide-react';
import { useDismissable } from '../hooks/useDismissable';
import { LoadingDots } from './LoadingDots';

interface Option {
  id: string;
  name: string;
}

/** One dropdown row: 12px vertical padding ×2 + ~17px text line. */
const OPTION_ROW_HEIGHT = 41;
/** The dropdown shows at most this many options before scrolling. */
const MAX_VISIBLE_OPTIONS = 5;

interface ModelSelectProps {
  options: Option[];
  value: string;
  onChange: (id: string) => void;
  height?: number;
  /** Grays out and blocks all interaction (e.g. signed out; switching
      models needs an authenticated native download). */
  disabled?: boolean;
  /** The catalog fetch is in flight (renders a dots row in the dropdown). */
  loading?: boolean;
  /** Catalog total for the "n/N" display. */
  totalCount: number;
}

export const ModelSelect: React.FC<ModelSelectProps> = ({
  options,
  value,
  onChange,
  height = 46,
  disabled = false,
  loading = false,
  totalCount,
}) => {
  const [isOpen, setIsOpen] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);
  const dropdownRef = useRef<HTMLDivElement>(null);

  const currentIndex = options.findIndex((opt) => opt.id === value);
  const selectedOption = options[currentIndex];
  const positionText = `${currentIndex + 1}/${totalCount}`;

  const handlePrev = (e: React.MouseEvent) => {
    e.stopPropagation();
    if (currentIndex > 0) {
      onChange(options[currentIndex - 1].id);
    }
  };

  const handleNext = (e: React.MouseEvent) => {
    e.stopPropagation();
    if (currentIndex < options.length - 1) {
      onChange(options[currentIndex + 1].id);
    }
  };

  const handleSelect = (id: string) => {
    onChange(id);
    setIsOpen(false);
  };

  const close = useCallback(() => setIsOpen(false), []);
  useDismissable(isOpen, containerRef, close);

  return (
    <div
      ref={containerRef}
      style={{
        position: 'relative',
        width: '100%',
        opacity: disabled ? 0.45 : 1,
        pointerEvents: disabled ? 'none' : 'auto',
      }}
    >
      <div
        style={{
          borderRadius: '8px',
          background: 'rgba(120, 120, 128, 0.36)',
          height: `${height}px`,
          padding: '0 12px',
          display: 'flex',
          alignItems: 'center',
          width: '100%',
          boxSizing: 'border-box',
          gap: '10px',
          userSelect: 'none',
        }}
      >
        {/* Previous button */}
        <button
          onClick={handlePrev}
          disabled={currentIndex <= 0}
          style={{
            background: 'none',
            border: 'none',
            padding: '12px 0',
            cursor: currentIndex > 0 ? 'pointer' : 'default',
            opacity: currentIndex > 0 ? 1 : 0.4,
            display: 'flex',
            alignItems: 'center',
            color: 'white',
          }}
        >
          <ChevronLeft size={20} />
        </button>

        {/* Name / Dropdown trigger */}
        <div
          onClick={() => setIsOpen(!isOpen)}
          style={{
            flex: 1,
            minWidth: 0,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            gap: '6px',
            overflow: 'hidden',
            cursor: 'pointer',
            padding: '12px 0',
          }}
        >
          <span
            style={{
              overflow: 'hidden',
              whiteSpace: 'nowrap',
              textOverflow: 'ellipsis',
              color: 'white',
              fontSize: '14px',
              fontWeight: '400',
            }}
          >
            {selectedOption?.name ?? 'Select models...'}
          </span>
        </div>

        {/* Next button */}
        <button
          onClick={handleNext}
          disabled={currentIndex >= options.length - 1}
          style={{
            background: 'none',
            border: 'none',
            padding: '12px 0',
            cursor: currentIndex < options.length - 1 ? 'pointer' : 'default',
            opacity: currentIndex < options.length - 1 ? 1 : 0.4,
            display: 'flex',
            alignItems: 'center',
            color: 'white',
          }}
        >
          <ChevronRight size={20} />
        </button>

        {/* Divider + model count */}
        <div
          style={{
            width: '1px',
            alignSelf: 'stretch',
            margin: '8px 0',
            backgroundColor: 'rgba(84, 84, 88, 0.65)',
            flexShrink: 0,
          }}
        />
        <span
          style={{
            display: 'flex',
            alignItems: 'center',
            gap: '6px',
            color: 'rgba(255, 255, 255, 0.6)',
            fontSize: '13px',
            fontWeight: '400',
            whiteSpace: 'nowrap',
            flexShrink: 0,
          }}
        >
          <FolderClosed size={14} />
          {positionText}
        </span>
      </div>

      {/* Dropdown opens upward: the select sits at the bottom of the card,
          so a downward list would render past the card edge and get clipped. */}
      {isOpen && (
        <div
          ref={dropdownRef}
          style={{
            position: 'absolute',
            bottom: '100%',
            left: 0,
            right: 0,
            marginBottom: '4px',
            borderRadius: '8px',
            background: '#39393D',
            // 6 rows + their 1px dividers; anything longer scrolls.
            maxHeight: `${MAX_VISIBLE_OPTIONS * OPTION_ROW_HEIGHT + (MAX_VISIBLE_OPTIONS - 1)}px`,
            overflowY: 'auto',
            zIndex: 1000,
          }}
        >
          {options.map((option, index) => (
            <div
              key={option.id}
              onClick={() => handleSelect(option.id)}
              style={{
                padding: '12px 16px',
                cursor: 'pointer',
                color: 'white',
                fontSize: '14px',
                lineHeight: '17px',
                fontWeight: '400',
                overflow: 'hidden',
                textOverflow: 'ellipsis',
                whiteSpace: 'nowrap',
                background: option.id === value ? 'rgba(255, 255, 255, 0.1)' : 'transparent',
                borderBottom:
                  index < options.length - 1 ? '1px solid rgba(84, 84, 88, 0.65)' : 'none',
              }}
              onMouseEnter={(e) => {
                if (option.id !== value) {
                  e.currentTarget.style.background = 'rgba(255, 255, 255, 0.1)';
                }
              }}
              onMouseLeave={(e) => {
                if (option.id !== value) {
                  e.currentTarget.style.background = 'transparent';
                }
              }}
            >
              {option.name}
            </div>
          ))}
          {loading && (
            <div
              style={{
                display: 'flex',
                justifyContent: 'center',
                padding: '10px 16px',
                borderTop: '1px solid rgba(84, 84, 88, 0.65)',
              }}
            >
              <LoadingDots />
            </div>
          )}
        </div>
      )}
    </div>
  );
};
