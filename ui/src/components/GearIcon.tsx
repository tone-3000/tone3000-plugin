import React from 'react';
import { GRAY, SURFACE, WHITE } from './theme';
import { rem } from '../hooks/useUiScale';

/**
 * Gear-type glyphs from the Figma plugin kit (TONE3000-Web, 20px chips).
 * Authored at the chip size so outline weight stays opaque when rendered
 * small; `color` tints fill and stroke with no extra alpha.
 *
 * Used as:
 * - gear chip filters in the tone browser (small, GRAY / WHITE);
 * - fallback artwork when a tone has no image (large, gray on SURFACE;
 *   the web's ICON_BG_COLOR_MAP is the same #151517).
 */

interface Props {
  size?: number;
  color?: string;
}

const svgStyle = (size: number): React.CSSProperties => ({
  width: rem(size),
  height: rem(size),
  overflow: 'visible',
  display: 'block',
  flexShrink: 0,
});

/** Center a Figma glyph (authored smaller than 20×20) in the chip box. */
const center = (w: number, h: number) => `translate(${(20 - w) / 2}, ${(20 - h) / 2})`;

const FullRig = ({ size = 40, color = GRAY }: Props) => (
  <svg viewBox="0 0 20 20" fill="none" aria-label="Amp + Cab" style={svgStyle(size)}>
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M5.84586 2.16118V3.32247H13.9747V2.16118H5.84586ZM4.6846 2.05652C4.6846 1.47295 5.15767 0.99992 5.74123 0.99992H14.0794C14.6629 0.99992 15.136 1.473 15.136 2.05652V3.4271C15.136 4.01063 14.663 4.48373 14.0794 4.48373H5.74123C5.15765 4.48373 4.6846 4.01068 4.6846 3.4271V2.05652Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M3 6.22601C3 5.58467 3.51991 5.06475 4.16126 5.06475H15.7739C16.4152 5.06475 16.9351 5.58467 16.9351 6.22601V17.8386C16.9351 18.48 16.4152 18.9999 15.7739 18.9999H4.16126C3.51991 18.9999 3 18.48 3 17.8386V6.22601ZM15.7739 6.22601H4.16126V17.8386H15.7739V6.22601Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M5.31886 9.41951C5.31886 8.29715 6.22871 7.3873 7.35107 7.3873C8.47343 7.3873 9.38328 8.29715 9.38328 9.41951C9.38328 10.5419 8.47343 11.4517 7.35107 11.4517C6.22871 11.4517 5.31886 10.5419 5.31886 9.41951Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M5.31886 14.6452C5.31886 13.5228 6.22871 12.613 7.35107 12.613C8.47343 12.613 9.38328 13.5228 9.38328 14.6452C9.38328 15.7676 8.47343 16.6774 7.35107 16.6774C6.22871 16.6774 5.31886 15.7676 5.31886 14.6452Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M10.5445 9.41951C10.5445 8.29715 11.4544 7.3873 12.5768 7.3873C13.6991 7.3873 14.609 8.29715 14.609 9.41951C14.609 10.5419 13.6991 11.4517 12.5768 11.4517C11.4544 11.4517 10.5445 10.5419 10.5445 9.41951Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M10.5445 14.6452C10.5445 13.5228 11.4544 12.613 12.5768 12.613C13.6991 12.613 14.609 13.5228 14.609 14.6452C14.609 15.7676 13.6991 16.6774 12.5768 16.6774C11.4544 16.6774 10.5445 15.7676 10.5445 14.6452Z"
      fill={color}
    />
  </svg>
);

const Amp = ({ size = 40, color = GRAY }: Props) => (
  <svg viewBox="0 0 20 20" fill="none" aria-label="Amp" style={svgStyle(size)}>
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M2.775 6.2C2.07084 6.2 1.5 6.77084 1.5 7.475V13.9917C1.5 14.6958 2.07084 15.2667 2.775 15.2667H17.225C17.9292 15.2667 18.5 14.6958 18.5 13.9917V7.475C18.5 6.77084 17.9292 6.2 17.225 6.2H2.775ZM3.48333 7.33333C3.01389 7.33333 2.63333 7.71389 2.63333 8.18333V13.2833C2.63333 13.7528 3.01389 14.1333 3.48333 14.1333H16.5167C16.9861 14.1333 17.3667 13.7528 17.3667 13.2833V8.18333C17.3667 7.71389 16.9861 7.33333 16.5167 7.33333H3.48333Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M6.38141 7.21383C6.38141 7.21383 6.38156 7.21372 6.03334 6.76667C5.68512 6.31961 5.68532 6.31946 5.68532 6.31946L5.68622 6.31876L5.68812 6.31729L5.69428 6.31254L5.71591 6.29604C5.73437 6.28203 5.76086 6.26215 5.79473 6.23723C5.86243 6.1874 5.95983 6.11729 6.08183 6.03365C6.32531 5.86674 6.66955 5.64407 7.07342 5.42076C7.85935 4.9862 8.95719 4.5 10 4.5C11.0428 4.5 12.1407 4.9862 12.9266 5.42076C13.3305 5.64407 13.6747 5.86674 13.9182 6.03365C14.0402 6.11729 14.1376 6.1874 14.2053 6.23723C14.2392 6.26215 14.2656 6.28203 14.2841 6.29604L14.3057 6.31254L14.3119 6.31729L14.3138 6.31876L14.3144 6.31927C14.3144 6.31927 14.3149 6.31961 13.9667 6.76667C13.6185 7.21372 13.6186 7.21383 13.6186 7.21383L13.615 7.21104L13.5993 7.19911C13.585 7.18821 13.5628 7.17157 13.5335 7.15001C13.4749 7.10687 13.3879 7.04417 13.2774 6.96843C13.0559 6.81659 12.743 6.61427 12.3782 6.41258C11.6268 5.99713 10.7413 5.63333 10 5.63333C9.25867 5.63333 8.37318 5.99713 7.62182 6.41258C7.25704 6.61427 6.94414 6.81659 6.72264 6.96843C6.61216 7.04417 6.52511 7.10687 6.46649 7.15001C6.43719 7.17157 6.41504 7.18821 6.40067 7.19911L6.38503 7.21104L6.38141 7.21383Z"
      fill={color}
    />
    <path
      d="M14.5 10.5C15.0523 10.5 15.5 10.9477 15.5 11.5V14.5H4.5V11.5C4.5 10.9477 4.94772 10.5 5.5 10.5H14.5ZM7 11.5C6.58579 11.5 6.25 11.8358 6.25 12.25C6.25 12.6642 6.58579 13 7 13C7.41421 13 7.75 12.6642 7.75 12.25C7.75 11.8358 7.41421 11.5 7 11.5ZM10 11.5C9.58579 11.5 9.25 11.8358 9.25 12.25C9.25 12.6642 9.58579 13 10 13C10.4142 13 10.75 12.6642 10.75 12.25C10.75 11.8358 10.4142 11.5 10 11.5ZM13 11.5C12.5858 11.5 12.25 11.8358 12.25 12.25C12.25 12.6642 12.5858 13 13 13C13.4142 13 13.75 12.6642 13.75 12.25C13.75 11.8358 13.4142 11.5 13 11.5Z"
      fill={color}
    />
  </svg>
);

const Pedal = ({ size = 40, color = GRAY }: Props) => (
  <svg viewBox="0 0 20 20" fill="none" aria-label="Pedal" style={svgStyle(size)}>
    <g transform={center(11.8953, 16.5)}>
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M0.767442 2.11047C0.767442 0.94489 1.71233 0 2.87791 0H9.01744C10.183 0 11.1279 0.94489 11.1279 2.11047V14.3895C11.1279 15.5551 10.183 16.5 9.01744 16.5H2.87791C1.71233 16.5 0.767442 15.5551 0.767442 14.3895V2.11047ZM2.87791 1.15116C2.3481 1.15116 1.9186 1.58065 1.9186 2.11047V14.3895C1.9186 14.9194 2.3481 15.3488 2.87791 15.3488H9.01744C9.54728 15.3488 9.97674 14.9194 9.97674 14.3895V2.11047C9.97674 1.58065 9.54728 1.15116 9.01744 1.15116H2.87791Z"
        fill={color}
      />
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M0.767442 6.71512C0.767442 5.97338 1.36873 5.37209 2.11047 5.37209H9.78488C10.5266 5.37209 11.1279 5.97338 11.1279 6.71512V14.3895C11.1279 15.5551 10.183 16.5 9.01744 16.5H2.87791C1.71233 16.5 0.767442 15.5551 0.767442 14.3895V6.71512ZM2.11047 6.52326C2.0045 6.52326 1.9186 6.60916 1.9186 6.71512V14.3895C1.9186 14.9194 2.3481 15.3488 2.87791 15.3488H9.01744C9.54728 15.3488 9.97674 14.9194 9.97674 14.3895V6.71512C9.97674 6.60916 9.89087 6.52326 9.78488 6.52326H2.11047Z"
        fill={color}
      />
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M3.06977 10.5523C3.06977 10.2345 3.32747 9.97674 3.64535 9.97674H8.25C8.56787 9.97674 8.82558 10.2345 8.82558 10.5523V13.6221C8.82558 13.94 8.56787 14.1977 8.25 14.1977H3.64535C3.32747 14.1977 3.06977 13.94 3.06977 13.6221V10.5523Z"
        fill={color}
      />
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M9.97674 7.48256C9.97674 7.16468 10.2345 6.90698 10.5523 6.90698H11.3198C11.6376 6.90698 11.8953 7.16468 11.8953 7.48256V9.01744C11.8953 9.33532 11.6376 9.59302 11.3198 9.59302H10.5523C10.2345 9.59302 9.97674 9.33532 9.97674 9.01744V7.48256Z"
        fill={color}
      />
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M0 7.48256C0 7.16468 0.257699 6.90698 0.575581 6.90698H1.34302C1.66091 6.90698 1.9186 7.16468 1.9186 7.48256V9.01744C1.9186 9.33532 1.66091 9.59302 1.34302 9.59302H0.575581C0.257699 9.59302 0 9.33532 0 9.01744V7.48256Z"
        fill={color}
      />
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M7.86628 1.9186C7.12455 1.9186 6.52326 2.5199 6.52326 3.26163C6.52326 4.00336 7.12455 4.60465 7.86628 4.60465C8.60801 4.60465 9.2093 4.00336 9.2093 3.26163C9.2093 2.5199 8.60801 1.9186 7.86628 1.9186Z"
        fill={color}
      />
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M4.02907 1.9186C3.28734 1.9186 2.68605 2.5199 2.68605 3.26163C2.68605 4.00336 3.28734 4.60465 4.02907 4.60465C4.7708 4.60465 5.37209 4.00336 5.37209 3.26163C5.37209 2.5199 4.7708 1.9186 4.02907 1.9186Z"
        fill={color}
      />
    </g>
  </svg>
);

const Outboard = ({ size = 40, color = GRAY }: Props) => (
  <svg viewBox="0 0 20 20" fill="none" aria-label="Outboard" style={svgStyle(size)}>
    <g transform={center(18, 7.20002)}>
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M1.8 1.20002C1.46861 1.20002 1.2 1.46863 1.2 1.80002V5.40002C1.2 5.73137 1.46863 6.00002 1.8 6.00002H16.2C16.5314 6.00002 16.8 5.73137 16.8 5.40002V1.80002C16.8 1.46863 16.5314 1.20002 16.2 1.20002H1.8ZM0 1.80002C0 0.80589 0.805869 2.0504e-05 1.8 2.0504e-05H16.2C17.1941 2.0504e-05 18 0.80589 18 1.80002V5.40002C18 6.39407 17.1942 7.20002 16.2 7.20002H1.8C0.80585 7.20002 0 6.39407 0 5.40002V1.80002Z"
        fill={color}
      />
      <path
        d="M3.59968 4.80042C4.26242 4.80042 4.79968 4.26317 4.79968 3.60042C4.79968 2.93768 4.26242 2.40042 3.59968 2.40042C2.93694 2.40042 2.39968 2.93768 2.39968 3.60042C2.39968 4.26317 2.93694 4.80042 3.59968 4.80042Z"
        fill={color}
      />
      <path
        d="M7.19972 4.80042C7.86246 4.80042 8.39972 4.26317 8.39972 3.60042C8.39972 2.93768 7.86246 2.40042 7.19972 2.40042C6.53698 2.40042 5.99972 2.93768 5.99972 3.60042C5.99972 4.26317 6.53698 4.80042 7.19972 4.80042Z"
        fill={color}
      />
      <path
        d="M14.3997 2.40042H11.3997C10.7369 2.40042 10.1997 2.93768 10.1997 3.60042C10.1997 4.26317 10.7369 4.80042 11.3997 4.80042H14.3997C15.0624 4.80042 15.5997 4.26317 15.5997 3.60042C15.5997 2.93768 15.0624 2.40042 14.3997 2.40042Z"
        fill={color}
      />
    </g>
  </svg>
);

const SpeakerCab = ({ size = 40, color = GRAY }: Props) => (
  <svg viewBox="0 0 20 20" fill="none" aria-label="Cabinet" style={svgStyle(size)}>
    <g transform={center(14, 14)}>
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M0 1.16667C0 0.522334 0.522334 0 1.16667 0H12.8333C13.4777 0 14 0.522334 14 1.16667V12.8333C14 13.4777 13.4777 14 12.8333 14H1.16667C0.522334 14 0 13.4777 0 12.8333V1.16667ZM12.8333 1.16667H1.16667V12.8333H12.8333V1.16667Z"
        fill={color}
      />
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M2.32977 4.37503C2.32977 3.24744 3.24386 2.33336 4.37144 2.33336C5.49902 2.33336 6.41311 3.24744 6.41311 4.37503C6.41311 5.50261 5.49902 6.41669 4.37144 6.41669C3.24386 6.41669 2.32977 5.50261 2.32977 4.37503Z"
        fill={color}
      />
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M2.32977 9.62503C2.32977 8.49745 3.24386 7.58336 4.37144 7.58336C5.49902 7.58336 6.41311 8.49745 6.41311 9.62503C6.41311 10.7526 5.49902 11.6667 4.37144 11.6667C3.24386 11.6667 2.32977 10.7526 2.32977 9.62503Z"
        fill={color}
      />
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M7.57977 4.37503C7.57977 3.24744 8.49386 2.33336 9.62144 2.33336C10.749 2.33336 11.6631 3.24744 11.6631 4.37503C11.6631 5.50261 10.749 6.41669 9.62144 6.41669C8.49386 6.41669 7.57977 5.50261 7.57977 4.37503Z"
        fill={color}
      />
      <path
        fillRule="evenodd"
        clipRule="evenodd"
        d="M7.57977 9.62503C7.57977 8.49745 8.49386 7.58336 9.62144 7.58336C10.749 7.58336 11.6631 8.49745 11.6631 9.62503C11.6631 10.7526 10.749 11.6667 9.62144 11.6667C8.49386 11.6667 7.57977 10.7526 7.57977 9.62503Z"
        fill={color}
      />
    </g>
  </svg>
);

const Space = ({ size = 40, color = GRAY }: Props) => (
  <svg viewBox="0 0 20 20" fill="none" aria-label="Spaces" style={svgStyle(size)}>
    <g transform={center(15.6164, 15)}>
      <path d="M7.07743 13.2439H0.5" stroke={color} strokeLinecap="round" />
      <path
        d="M7.07715 1.96143V13.7694C7.07718 13.8804 7.1025 13.9899 7.15119 14.0897C7.19987 14.1895 7.27065 14.2768 7.35814 14.3452C7.44563 14.4135 7.54753 14.461 7.65612 14.484C7.76471 14.5071 7.87712 14.5051 7.98483 14.4783L12.9238 13.2439V2.69226C12.9237 2.36631 12.8147 2.04974 12.6141 1.79285C12.4135 1.53596 12.1328 1.35349 11.8166 1.27445L8.89325 0.543628C8.67784 0.489784 8.45299 0.485724 8.23578 0.531756C8.01856 0.577788 7.81469 0.672702 7.63964 0.809293C7.46458 0.945885 7.32295 1.12056 7.22548 1.32007C7.12802 1.51957 7.07729 1.73939 7.07715 1.96143Z"
        stroke={color}
        strokeLinecap="round"
      />
      <path
        d="M7.0773 1.55072H4.88483C4.49717 1.55072 4.1254 1.70472 3.85128 1.97883C3.57717 2.25294 3.42318 2.62472 3.42318 3.01237V13.2439"
        stroke={color}
        strokeLinecap="round"
      />
      <path d="M9.26953 7.39733H9.27788" stroke={color} strokeLinecap="round" />
      <path d="M15.1164 13.2439H12.9239" stroke={color} strokeLinecap="round" />
    </g>
  </svg>
);

const Experimental = ({ size = 40, color = GRAY }: Props) => (
  <svg viewBox="0 0 20 20" fill="none" aria-label="Experimental" style={svgStyle(size)}>
    <g transform={center(13, 13)}>
      <path
        d="M6.53223 7.36574C6.99246 7.36574 7.36556 6.99265 7.36556 6.53241C7.36556 6.07217 6.99246 5.69908 6.53223 5.69908C6.07199 5.69908 5.69889 6.07217 5.69889 6.53241C5.69889 6.99265 6.07199 7.36574 6.53223 7.36574Z"
        fill={color}
        stroke={color}
        strokeLinecap="round"
        strokeLinejoin="round"
      />
      <path
        d="M11.9694 11.9694C13.3301 10.6154 11.9827 7.06028 8.9679 4.0321C5.93972 1.01725 2.38461 -0.330088 1.03059 1.03059C-0.330088 2.38461 1.01725 5.93972 4.0321 8.9679C7.06028 11.9827 10.6154 13.3301 11.9694 11.9694Z"
        stroke={color}
        strokeLinecap="round"
        strokeLinejoin="round"
      />
      <path
        d="M8.9679 8.9679C11.9827 5.93972 13.3301 2.38461 11.9694 1.03059C10.6154 -0.330088 7.06028 1.01725 4.0321 4.0321C1.01725 7.06028 -0.330088 10.6154 1.03059 11.9694C2.38461 13.3301 5.93972 11.9827 8.9679 8.9679Z"
        stroke={color}
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </g>
  </svg>
);

const Ir = ({ size = 40, color = GRAY }: Props) => (
  <svg viewBox="0 0 40 40" fill="none" aria-label="Impulse Response" style={svgStyle(size)}>
    <path
      d="M35 20H31.28C30.6245 19.9986 29.9865 20.212 29.4637 20.6075C28.9409 21.0029 28.562 21.5588 28.385 22.19L24.86 34.73C24.8373 34.8079 24.7899 34.8763 24.725 34.925C24.6601 34.9737 24.5811 35 24.5 35C24.4189 35 24.3399 34.9737 24.275 34.925C24.2101 34.8763 24.1627 34.8079 24.14 34.73L15.86 5.27C15.8373 5.19211 15.7899 5.12368 15.725 5.075C15.6601 5.02632 15.5811 5 15.5 5C15.4189 5 15.3399 5.02632 15.275 5.075C15.2101 5.12368 15.1627 5.19211 15.14 5.27L11.615 17.81C11.4387 18.4387 11.0621 18.9928 10.5423 19.388C10.0225 19.7833 9.38798 19.9981 8.735 20H5"
      stroke={color}
      strokeWidth="3"
      strokeLinecap="round"
      strokeLinejoin="round"
    />
  </svg>
);

/** Mirrors the web's ICON_MAP (deprecated `full-rig` shares the Amp+Cab glyph). */
const ICONS: Record<string, React.FC<Props>> = {
  'amp-cab': FullRig,
  'full-rig': FullRig,
  amp: Amp,
  pedal: Pedal,
  outboard: Outboard,
  cab: SpeakerCab,
  space: Space,
  experimental: Experimental,
  ir: Ir,
};

/** Gear-type icon by TONE3000 gear id; unknown/missing gear falls back to the
    amp glyph (web behavior). Pass color="currentColor" to follow text color. */
export const GearIcon: React.FC<{ gear?: string; size?: number; color?: string }> = ({
  gear,
  size = 40,
  color = GRAY,
}) => {
  const Icon = ICONS[gear?.toLowerCase() ?? ''] ?? Amp;
  return <Icon size={size} color={color} />;
};

/**
 * Fallback artwork for tones without an image: the gear glyph centered on the
 * web's icon background (#151517 = SURFACE). Defaults to ~40% of the box like
 * the web's ToneCard/ToneImage fallbacks; pass `iconSize` for a fixed glyph
 * (gallery tiles use 64). Fills its parent.
 */
export const GearImageFallback: React.FC<{
  gear?: string;
  boxSize: number;
  iconSize?: number;
}> = ({ gear, boxSize, iconSize }) => (
  <div
    style={{
      width: '100%',
      height: '100%',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      backgroundColor: SURFACE,
    }}
  >
    <GearIcon gear={gear} size={iconSize ?? Math.round(boxSize * 0.4)} />
  </div>
);

/**
 * Tone artwork with recovery: renders the image URL when present and swaps in
 * the gear-glyph fallback if it's missing or the network fetch fails (offline
 * / tone3000.com down). Local-file blocks (drag-and-drop loads) show the
 * tone's name and a NAM/IR tag instead: there is no artwork and no gear id to
 * fall back to. Fills its parent like a plain cover <img>.
 */
export const ToneImage: React.FC<{
  src: string | undefined;
  alt: string;
  gear?: string;
  local?: boolean;
  /** "nam" or "ir"; tags a local tone's tile, which has no artwork to
      identify it with. Ignored for catalog tones. */
  format?: string;
  boxSize: number;
  /** Override the fallback glyph size (defaults to ~40% of `boxSize`). */
  iconSize?: number;
  draggable?: boolean;
}> = ({ src, alt, gear, local, format, boxSize, iconSize, draggable }) => {
  const [failed, setFailed] = React.useState(false);
  // A new URL (tone swap/model switch) gets a fresh chance to load.
  React.useEffect(() => setFailed(false), [src]);

  if (local) {
    // A local tone is a file on disk, not a catalog record: finishLocalToneLoad
    // builds it with no `images`, so there is never artwork to show here. A
    // glyph made every local tile identical — and the tile carries the title
    // nowhere else (it lives in the tooltip alone), so the name is the only
    // thing that can tell two filed captures apart.
    const nameSize = Math.max(11, Math.round(boxSize * 0.085));
    const tagSize = Math.max(9, Math.round(boxSize * 0.055));
    return (
      <div
        style={{
          width: '100%',
          height: '100%',
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          gap: rem(Math.round(nameSize * 0.45)),
          backgroundColor: SURFACE,
          // Keeps text off the edge at every tile size, and clear of the
          // quick-actions chrome that fades in over the top strip.
          padding: rem(Math.round(boxSize * 0.1)),
          boxSizing: 'border-box',
          overflow: 'hidden',
        }}
      >
        {format !== undefined && format !== '' && (
          <span
            style={{
              fontSize: rem(tagSize),
              fontWeight: 600,
              letterSpacing: rem(Math.max(0.5, tagSize * 0.08)),
              color: GRAY,
              textTransform: 'uppercase',
              flexShrink: 0,
            }}
          >
            {format.toLowerCase() === 'ir' ? 'IR' : 'NAM'}
          </span>
        )}
        <span
          style={{
            fontSize: rem(nameSize),
            fontWeight: 500,
            lineHeight: 1.25,
            color: WHITE,
            textAlign: 'center',
            // Capture filenames run long and often have no spaces to break
            // on, so they must wrap mid-word rather than overflow the tile.
            overflowWrap: 'anywhere',
            display: '-webkit-box',
            WebkitBoxOrient: 'vertical',
            WebkitLineClamp: 3,
            overflow: 'hidden',
          }}
        >
          {alt}
        </span>
      </div>
    );
  }

  if (!src || failed)
    return <GearImageFallback gear={gear} boxSize={boxSize} iconSize={iconSize} />;
  return (
    <img
      src={src}
      alt={alt}
      draggable={draggable}
      onError={() => setFailed(true)}
      style={{ width: '100%', height: '100%', objectFit: 'cover', display: 'block' }}
    />
  );
};
