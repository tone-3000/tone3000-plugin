import * as React from 'react';
import type { LucideIcon, LucideProps } from 'lucide-react';
import {
  ArrowLeft as LArrowLeft,
  ArrowLeftRight as LArrowLeftRight,
  ArrowRight as LArrowRight,
  ArrowUpDown as LArrowUpDown,
  Bluetooth as LBluetooth,
  Bookmark as LBookmark,
  Check as LCheck,
  ChevronDown as LChevronDown,
  ChevronLeft as LChevronLeft,
  ChevronRight as LChevronRight,
  ChevronUp as LChevronUp,
  Circle as LCircle,
  ClipboardPaste as LClipboardPaste,
  Copy as LCopy,
  Download as LDownload,
  Equal as LEqual,
  ExternalLink as LExternalLink,
  File as LFile,
  FolderClosed as LFolderClosed,
  FolderPlus as LFolderPlus,
  Gauge as LGauge,
  GripVertical as LGripVertical,
  Info as LInfo,
  Laptop as LLaptop,
  Link as LLink,
  LogIn as LLogIn,
  LogOut as LLogOut,
  MidiPort as LMidiPort,
  Pencil as LPencil,
  Plus as LPlus,
  PlusCircle as LPlusCircle,
  Power as LPower,
  Redo2 as LRedo2,
  RotateCcw as LRotateCcw,
  Save as LSave,
  Search as LSearch,
  Settings as LSettings,
  Share as LShare,
  ShieldAlert as LShieldAlert,
  Trash2 as LTrash2,
  Undo2 as LUndo2,
  Upload as LUpload,
  Volume2 as LVolume2,
  WifiOff as LWifiOff,
  X as LX,
} from 'lucide-react';
import { rem } from '../hooks/useUiScale';

/**
 * Design-space wrappers for lucide icons. Lucide's `size` prop lands as
 * width/height *attributes* in raw px; these wrappers restate the size as
 * rem styles (1rem = 1 design px, see useUiScale) so glyphs scale with the
 * UI like every other length. The stroke is authored in viewBox units, so
 * it scales with the rendered box on its own.
 *
 * Import icons from here, never from 'lucide-react' directly.
 */
type IconProps = Omit<LucideProps, 'size'> & { size?: number };

const scaled = (Icon: LucideIcon, name: string): React.FC<IconProps> => {
  const Scaled: React.FC<IconProps> = ({ size = 24, style, ...rest }) => (
    <Icon size={size} {...rest} style={{ width: rem(size), height: rem(size), ...style }} />
  );
  Scaled.displayName = name;
  return Scaled;
};

export const ArrowLeft = scaled(LArrowLeft, 'ArrowLeft');
export const ArrowLeftRight = scaled(LArrowLeftRight, 'ArrowLeftRight');
export const ArrowRight = scaled(LArrowRight, 'ArrowRight');
export const ArrowUpDown = scaled(LArrowUpDown, 'ArrowUpDown');
export const Bluetooth = scaled(LBluetooth, 'Bluetooth');
export const Bookmark = scaled(LBookmark, 'Bookmark');
export const Check = scaled(LCheck, 'Check');
export const ChevronDown = scaled(LChevronDown, 'ChevronDown');
export const ChevronLeft = scaled(LChevronLeft, 'ChevronLeft');
export const ChevronRight = scaled(LChevronRight, 'ChevronRight');
export const ChevronUp = scaled(LChevronUp, 'ChevronUp');
export const Circle = scaled(LCircle, 'Circle');
export const ClipboardPaste = scaled(LClipboardPaste, 'ClipboardPaste');
export const Copy = scaled(LCopy, 'Copy');
export const Download = scaled(LDownload, 'Download');
export const Equal = scaled(LEqual, 'Equal');
export const ExternalLink = scaled(LExternalLink, 'ExternalLink');
export const File = scaled(LFile, 'File');
export const FolderClosed = scaled(LFolderClosed, 'FolderClosed');
export const FolderPlus = scaled(LFolderPlus, 'FolderPlus');
export const Gauge = scaled(LGauge, 'Gauge');
export const GripVertical = scaled(LGripVertical, 'GripVertical');
export const Info = scaled(LInfo, 'Info');
export const Laptop = scaled(LLaptop, 'Laptop');
export const Link = scaled(LLink, 'Link');
export const LogIn = scaled(LLogIn, 'LogIn');
export const LogOut = scaled(LLogOut, 'LogOut');
export const MidiPort = scaled(LMidiPort, 'MidiPort');
export const Pencil = scaled(LPencil, 'Pencil');
export const Plus = scaled(LPlus, 'Plus');
export const PlusCircle = scaled(LPlusCircle, 'PlusCircle');
export const Power = scaled(LPower, 'Power');
export const Redo2 = scaled(LRedo2, 'Redo2');
export const RotateCcw = scaled(LRotateCcw, 'RotateCcw');
export const Save = scaled(LSave, 'Save');
export const Search = scaled(LSearch, 'Search');
export const Settings = scaled(LSettings, 'Settings');
export const Share = scaled(LShare, 'Share');
export const ShieldAlert = scaled(LShieldAlert, 'ShieldAlert');
export const Trash2 = scaled(LTrash2, 'Trash2');
export const Undo2 = scaled(LUndo2, 'Undo2');
export const Upload = scaled(LUpload, 'Upload');
export const Volume2 = scaled(LVolume2, 'Volume2');
export const WifiOff = scaled(LWifiOff, 'WifiOff');
export const X = scaled(LX, 'X');
