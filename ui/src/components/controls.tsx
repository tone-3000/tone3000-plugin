import React, { useCallback, useRef, useState } from 'react';
import { ChevronDown } from 'lucide-react';
import { useDismissable } from '../hooks/useDismissable';
import { MUTED, SUBTLE, BRAND_RED, BRAND_YELLOW, WHITE } from './theme';

/**
 * Shared form primitives for settings-style surfaces (Settings takeover,
 * System Settings tab, app banners). Extracted from Settings so every field
 * in the product renders identically: outlined black fields, 1px zinc-700
 * borders, green pill switches, white radio/check indicators.
 */

export const FIELD_BORDER = '1px solid #3f3f46';

export const outlinedFieldStyle: React.CSSProperties = {
  backgroundColor: 'transparent',
  border: FIELD_BORDER,
  borderRadius: '6px',
  color: '#ffffff',
  fontSize: '14px',
  fontWeight: 400,
  outline: 'none',
  boxSizing: 'border-box',
};

// Only headers carry weight; everything else is regular (the app's global
// stylesheet defaults heavier, so body copy sets 400 explicitly).
export const sectionLabelStyle: React.CSSProperties = {
  fontSize: '15px',
  fontWeight: 600,
  color: '#ffffff',
};

export const descriptionStyle: React.CSSProperties = {
  fontSize: '13px',
  fontWeight: 400,
  color: MUTED,
  margin: '6px 0 0',
  lineHeight: 1.45,
};

export const ctaButtonStyle: React.CSSProperties = {
  width: '100%',
  padding: '12px 16px',
  borderRadius: '10px',
  border: '1px solid #ffffff',
  backgroundColor: 'transparent',
  color: '#ffffff',
  fontSize: '15px',
  fontWeight: 400,
  cursor: 'pointer',
  textAlign: 'center',
};

/** Fine-print caption under a field (locked notes, hints). */
export const captionStyle: React.CSSProperties = {
  ...descriptionStyle,
  fontSize: '12px',
};

/** Green pill switch mirroring the web ToggleSimple: 40×24 track (zinc-500
    off, #00D13B on), 16px white knob with a 4px inset, 300ms ease. */
export const PillToggle: React.FC<{ value: boolean; onChange: (value: boolean) => void }> = ({
  value,
  onChange,
}) => (
  <button
    role="switch"
    aria-checked={value}
    onClick={() => onChange(!value)}
    style={{
      position: 'relative',
      width: '40px',
      height: '24px',
      borderRadius: '12px',
      border: 'none',
      padding: 0,
      cursor: 'pointer',
      backgroundColor: value ? '#00D13B' : '#71717a',
      boxShadow: 'inset 0 2px 4px rgba(0, 0, 0, 0.15)',
      flexShrink: 0,
      transition: 'background-color 0.3s ease-in-out',
    }}
  >
    <span
      style={{
        position: 'absolute',
        top: '4px',
        left: '4px',
        width: '16px',
        height: '16px',
        borderRadius: '50%',
        backgroundColor: '#ffffff',
        boxShadow: '0 1px 2px rgba(0, 0, 0, 0.3)',
        transform: value ? 'translateX(16px)' : 'translateX(0)',
        transition: 'transform 0.3s ease-in-out',
        display: 'block',
      }}
    />
  </button>
);

/** Custom dropdown select styled like the plugin's other pickers: outlined
    trigger, dark panel, hover-highlight rows. Renders disabled (dimmed, no
    chevron interaction) for locked single-option lists; per the audio
    settings spec, a one-option select must never pretend to be a choice.
    A null value is the empty state: the trigger shows the dimmed
    placeholder and no option renders as selected. Options may carry a
    sublabel: smaller, dimmer context under the label (the MIDI mapping
    picker names each block slot's current tone this way); the trigger
    always shows the label alone. */
export function SelectField<T extends string>({
  value,
  options,
  onChange,
  disabled = false,
  placeholder,
  ariaLabel,
}: {
  value: T | null;
  options: { value: T; label: string; sublabel?: string }[];
  onChange: (value: T) => void;
  disabled?: boolean;
  /** Trigger text while value is null (or matches no option). */
  placeholder?: string;
  ariaLabel?: string;
}) {
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement | null>(null);
  const close = useCallback(() => setOpen(false), []);
  useDismissable(open, rootRef, close);

  const selected = options.find((option) => option.value === value);

  return (
    <div ref={rootRef} style={{ position: 'relative' }}>
      <button
        onClick={() => !disabled && setOpen((prev) => !prev)}
        disabled={disabled}
        aria-label={ariaLabel}
        style={{
          ...outlinedFieldStyle,
          width: '100%',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          gap: '10px',
          padding: '12px 16px',
          cursor: disabled ? 'default' : 'pointer',
          color: disabled || !selected ? MUTED : '#ffffff',
        }}
      >
        {selected?.label ?? placeholder ?? ''}
        {!disabled && (
          <ChevronDown
            size={16}
            style={{
              color: MUTED,
              flexShrink: 0,
              transform: open ? 'rotate(180deg)' : 'none',
              transition: 'transform 0.15s ease',
            }}
          />
        )}
      </button>

      {open && (
        <div
          style={{
            position: 'absolute',
            top: 'calc(100% + 4px)',
            left: 0,
            right: 0,
            borderRadius: '6px',
            background: '#000000',
            border: FIELD_BORDER,
            overflow: 'hidden auto',
            maxHeight: '264px',
            zIndex: 100,
          }}
        >
          {options.map((option) => (
            <div
              key={option.value}
              onClick={() => {
                onChange(option.value);
                setOpen(false);
              }}
              onMouseEnter={(e) => {
                if (option.value !== value)
                  e.currentTarget.style.background = 'rgba(255, 255, 255, 0.1)';
              }}
              onMouseLeave={(e) => {
                if (option.value !== value) e.currentTarget.style.background = 'transparent';
              }}
              style={{
                padding: '12px 16px',
                cursor: 'pointer',
                color: '#ffffff',
                fontSize: '14px',
                fontWeight: 400,
                // No dividers between rows; only the active/hover fill and the
                // container border delineate options.
                background: option.value === value ? 'rgba(255, 255, 255, 0.1)' : 'transparent',
              }}
            >
              {option.label}
              {option.sublabel && (
                <span
                  style={{
                    display: 'block',
                    fontSize: '11px',
                    fontWeight: 400,
                    color: SUBTLE,
                    marginTop: '2px',
                  }}
                >
                  {option.sublabel}
                </span>
              )}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

/** Label + help + control: the repeating field shape of the settings tabs. */
export const FieldRow: React.FC<{
  label: string;
  help?: React.ReactNode;
  labelExtra?: React.ReactNode;
  children: React.ReactNode;
}> = ({ label, help, labelExtra, children }) => (
  <div style={{ marginBottom: '32px' }}>
    <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
      <span style={sectionLabelStyle}>{label}</span>
      {labelExtra}
    </div>
    {help && <p style={{ ...descriptionStyle, marginBottom: '12px' }}>{help}</p>}
    {!help && <div style={{ height: '12px' }} />}
    {children}
  </div>
);

/** Section label with a pill toggle on the right, description underneath. */
export const ToggleRow: React.FC<{
  label: string;
  description: React.ReactNode;
  value: boolean;
  onChange: (value: boolean) => void;
  children?: React.ReactNode;
}> = ({ label, description, value, onChange, children }) => (
  <div style={{ marginBottom: '32px' }}>
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        gap: '16px',
      }}
    >
      <span style={sectionLabelStyle}>{label}</span>
      <PillToggle value={value} onChange={onChange} />
    </div>
    <p style={descriptionStyle}>{description}</p>
    {children}
  </div>
);

/** Round (radio) or rounded-square (check) selection indicator. */
export const ChoiceIndicator: React.FC<{ selected: boolean; square?: boolean }> = ({
  selected,
  square = false,
}) => (
  <span
    aria-hidden
    style={{
      width: '18px',
      height: '18px',
      borderRadius: square ? '5px' : '50%',
      border: `2px solid ${selected ? '#ffffff' : '#71717a'}`,
      boxSizing: 'border-box',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      flexShrink: 0,
    }}
  >
    {selected && (
      <span
        style={{
          width: '8px',
          height: '8px',
          borderRadius: square ? '2px' : '50%',
          backgroundColor: '#ffffff',
          display: 'block',
        }}
      />
    )}
  </span>
);

/** Radio row with label + description (NAM A2 Size options etc). */
export const RadioOption: React.FC<{
  selected: boolean;
  label: string;
  description: React.ReactNode;
  onSelect: () => void;
  children?: React.ReactNode;
}> = ({ selected, label, description, onSelect, children }) => (
  <div style={{ marginBottom: '16px' }}>
    <button
      type="button"
      role="radio"
      aria-checked={selected}
      onClick={onSelect}
      style={{
        display: 'flex',
        alignItems: 'flex-start',
        gap: '12px',
        width: '100%',
        padding: 0,
        border: 'none',
        background: 'transparent',
        cursor: 'pointer',
        textAlign: 'left',
        color: 'inherit',
      }}
    >
      <span style={{ marginTop: '1px', display: 'flex' }}>
        <ChoiceIndicator selected={selected} />
      </span>
      <span style={{ minWidth: 0, flex: 1 }}>
        <span
          style={{
            display: 'block',
            fontSize: '14px',
            fontWeight: 400,
            color: '#ffffff',
            lineHeight: 1.3,
          }}
        >
          {label}
        </span>
        <span
          style={{
            display: 'block',
            fontSize: '13px',
            fontWeight: 400,
            color: MUTED,
            marginTop: '4px',
            lineHeight: 1.45,
          }}
        >
          {description}
        </span>
      </span>
    </button>
    {children}
  </div>
);

/** Small segmented control (Mono / Stereo). */
export function SegmentedControl<T extends string>({
  value,
  options,
  onChange,
  ariaLabel,
}: {
  value: T;
  options: { value: T; label: string }[];
  onChange: (value: T) => void;
  ariaLabel?: string;
}) {
  return (
    <div
      role="radiogroup"
      aria-label={ariaLabel}
      style={{
        display: 'inline-flex',
        background: '#0a0a0a',
        border: FIELD_BORDER,
        borderRadius: '8px',
        padding: '2px',
      }}
    >
      {options.map((option) => {
        const selected = option.value === value;
        return (
          <button
            key={option.value}
            role="radio"
            aria-checked={selected}
            onClick={() => onChange(option.value)}
            style={{
              background: selected ? 'rgba(255, 255, 255, 0.16)' : 'transparent',
              border: 'none',
              color: selected ? '#ffffff' : MUTED,
              fontSize: '11px',
              fontWeight: 600,
              padding: '4px 12px',
              borderRadius: '6px',
              cursor: 'pointer',
            }}
          >
            {option.label}
          </button>
        );
      })}
    </div>
  );
}

//==============================================================================
// Alert banners

export type AlertVariant = 'error' | 'warn' | 'info';

const ALERT_COLORS: Record<AlertVariant, string> = {
  error: BRAND_RED,
  warn: BRAND_YELLOW,
  info: BRAND_YELLOW,
};

/** Circled "!" in the variant color; the shared alert glyph. */
export const AlertIcon: React.FC<{ variant: AlertVariant }> = ({ variant }) => (
  <span
    aria-hidden
    style={{
      flexShrink: 0,
      width: '16px',
      height: '16px',
      border: `1.6px solid ${ALERT_COLORS[variant]}`,
      borderRadius: '50%',
      color: ALERT_COLORS[variant],
      fontSize: '10px',
      lineHeight: 1,
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      fontWeight: 700,
    }}
  >
    !
  </span>
);

export interface AlertAction {
  label: string;
  onClick: () => void;
  /** Secondary = the muted "Ignore" style next to a primary action. */
  secondary?: boolean;
}

const alertActionStyle = (secondary: boolean): React.CSSProperties => ({
  background: 'none',
  border: secondary ? '1px solid #3f3f46' : `1px solid ${WHITE}`,
  color: secondary ? MUTED : WHITE,
  borderRadius: '7px',
  fontSize: '11.5px',
  fontWeight: 600,
  padding: '4px 11px',
  cursor: 'pointer',
  whiteSpace: 'nowrap',
  flexShrink: 0,
});

/**
 * Inline alert used in settings flows (feedback-risk warning, device errors):
 * rounded card, colored "!" glyph, white copy, optional actions. The
 * main-window banner bar shares the same glyph/copy language but lives in
 * AppBanner (bar layout + window-height coupling).
 */
export const AlertCard: React.FC<{
  variant: AlertVariant;
  children: React.ReactNode;
  actions?: AlertAction[];
  style?: React.CSSProperties;
}> = ({ variant, children, actions = [], style }) => (
  <div
    role="alert"
    style={{
      display: 'flex',
      gap: '10px',
      alignItems: 'flex-start',
      background: '#000000',
      border: FIELD_BORDER,
      borderRadius: '10px',
      padding: '11px 13px',
      fontSize: '12.5px',
      color: '#ffffff',
      lineHeight: 1.5,
      ...style,
    }}
  >
    <span style={{ marginTop: '1px', display: 'flex' }}>
      <AlertIcon variant={variant} />
    </span>
    <span style={{ flex: 1, minWidth: 0, fontWeight: 400 }}>{children}</span>
    {actions.map((action) => (
      <button
        key={action.label}
        onClick={action.onClick}
        style={alertActionStyle(action.secondary ?? false)}
      >
        {action.label}
      </button>
    ))}
  </div>
);
