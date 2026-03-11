import React, { useState, useRef, useEffect } from 'react';
import { ChevronLeft, ChevronRight } from 'lucide-react';

interface Option {
  id: string;
  name: string;
}

interface ModelSelectProps {
  options: Option[];
  value: string;
  onChange: (id: string) => void;
}

export const ModelSelect: React.FC<ModelSelectProps> = ({ options, value, onChange }) => {
  const [isOpen, setIsOpen] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);
  const dropdownRef = useRef<HTMLDivElement>(null);

  const currentIndex = options.findIndex((opt) => opt.id === value);
  const selectedOption = options[currentIndex];
  const positionText = `${currentIndex + 1}/${options.length}`;

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

  // Close dropdown when clicking outside
  useEffect(() => {
    const handleClickOutside = (event: MouseEvent) => {
      if (containerRef.current && !containerRef.current.contains(event.target as Node)) {
        setIsOpen(false);
      }
    };

    if (isOpen) {
      document.addEventListener('mousedown', handleClickOutside);
    }

    return () => {
      document.removeEventListener('mousedown', handleClickOutside);
    };
  }, [isOpen]);

  return (
    <div ref={containerRef} style={{ position: 'relative', width: '100%', maxWidth: '442px' }}>
      <div
        style={{
          borderRadius: '8px',
          background: 'rgba(120, 120, 128, 0.36)',
          height: '46px',
          padding: '0 16px',
          display: 'flex',
          alignItems: 'center',
          width: '100%',
          gap: '12px',
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

        {/* Position indicator */}
        <span
          style={{
            color: 'rgba(255, 255, 255, 0.6)',
            fontSize: '14px',
            minWidth: '40px',
            textAlign: 'right',
            fontWeight: '400',
          }}
        >
          {positionText}
        </span>
      </div>

      {/* Dropdown */}
      {isOpen && (
        <div
          ref={dropdownRef}
          style={{
            position: 'absolute',
            top: '100%',
            left: 0,
            right: 0,
            marginTop: '4px',
            borderRadius: '8px',
            background: '#39393D',
            maxHeight: '300px',
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
        </div>
      )}
    </div>
  );
};
