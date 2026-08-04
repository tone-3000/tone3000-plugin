import React, { useCallback, useRef, useState } from 'react';
import { LogIn, LogOut, Settings as SettingsIcon } from 'lucide-react';
import type { User } from '../types/tone';
import { AvatarImage } from './AvatarFallback';
import { useDismissable } from '../hooks/useDismissable';
import { HELP, helpProps } from './helpText';
import { BORDER, SURFACE_RAISED } from './theme';

/**
 * Account pill for the main header, a port of the web navbar's hamburger menu
 * (`Navlinks.tsx` HamburgerMenu): hamburger + avatar in a rounded-full
 * bordered button, opening a dark dropdown. Replaces the old settings icon;
 * Settings lives inside, alongside Logout when signed in.
 */

/** Web navbar hamburger glyph (Lucide `Menu` is too tall for the pill). */
const HamburgerIcon: React.FC = () => (
  <svg width="18" height="18" viewBox="0 0 24 24" fill="none">
    <path d="M4 6H20" stroke="white" strokeWidth="2" strokeLinecap="round" />
    <path d="M4 12H20" stroke="white" strokeWidth="2" strokeLinecap="round" />
    <path d="M4 18H20" stroke="white" strokeWidth="2" strokeLinecap="round" />
  </svg>
);

const itemStyle: React.CSSProperties = {
  display: 'flex',
  alignItems: 'center',
  gap: '12px',
  width: '100%',
  padding: '10px 12px',
  background: 'transparent',
  border: 'none',
  borderRadius: '8px',
  color: '#ffffff',
  fontSize: '14px',
  // Menu rows are body text: reset the global 600 default.
  fontWeight: 400,
  textAlign: 'left',
  cursor: 'pointer',
  whiteSpace: 'nowrap',
};

interface AccountMenuProps {
  user: User | null;
  authenticated: boolean;
  onOpenSettings: () => void;
  onLogin: () => void;
  onLogout: () => void;
}

export const AccountMenu: React.FC<AccountMenuProps> = ({
  user,
  authenticated,
  onOpenSettings,
  onLogin,
  onLogout,
}) => {
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement | null>(null);
  const close = useCallback(() => setOpen(false), []);
  useDismissable(open, rootRef, close);

  return (
    <div ref={rootRef} style={{ position: 'relative' }}>
      <style>{`.account-menu-item:hover { background-color: rgba(255, 255, 255, 0.08); }`}</style>
      <button
        onClick={() => setOpen((o) => !o)}
        {...helpProps(HELP.account)}
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: '10px',
          height: '40px',
          padding: '0 5px 0 12px',
          boxSizing: 'border-box',
          backgroundColor: 'transparent',
          border: BORDER,
          borderRadius: '9999px',
          cursor: 'pointer',
        }}
      >
        <HamburgerIcon />
        <div
          style={{
            width: '24px',
            height: '24px',
            borderRadius: '50%',
            overflow: 'hidden',
            flexShrink: 0,
          }}
        >
          <AvatarImage src={user?.avatar_url} alt={user?.username ?? ''} size={24} />
        </div>
      </button>

      {open && (
        <div
          style={{
            position: 'absolute',
            top: 'calc(100% + 8px)',
            right: 0,
            minWidth: '190px',
            backgroundColor: SURFACE_RAISED,
            border: BORDER,
            borderRadius: '12px',
            padding: '8px',
            display: 'flex',
            flexDirection: 'column',
            zIndex: 1000,
          }}
        >
          <button
            className="account-menu-item"
            style={itemStyle}
            onClick={() => {
              setOpen(false);
              onOpenSettings();
            }}
          >
            <SettingsIcon size={18} />
            Settings
          </button>
          {authenticated ? (
            <button
              className="account-menu-item"
              style={itemStyle}
              onClick={() => {
                setOpen(false);
                onLogout();
              }}
            >
              <LogOut size={18} />
              Logout
            </button>
          ) : (
            <button
              className="account-menu-item"
              style={itemStyle}
              onClick={() => {
                setOpen(false);
                onLogin();
              }}
            >
              <LogIn size={18} />
              Login
            </button>
          )}
        </div>
      )}
    </div>
  );
};
