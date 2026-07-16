import React from 'react';
import { GRAY, SURFACE } from './theme';

/**
 * Gear-type glyphs ported from the web app (tone3000 components/icons/Gear.tsx),
 * one per TONE3000 gear id. Used the same ways as on the web:
 * - gear chip filters in the tone browser (small, currentColor);
 * - fallback artwork when a tone has no image (large, gray on SURFACE —
 *   the web's ICON_BG_COLOR_MAP is the same #151517).
 */

interface Props {
  size?: number;
  color?: string;
}

const FullRig = ({ size = 40, color = GRAY }: Props) => (
  <svg width={size} height={size} viewBox="0 0 56 56" fill="none" aria-label="Amp + Cab">
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M16.3684 6.05152V9.30312H39.1292V6.05152H16.3684ZM13.1169 5.75846C13.1169 4.12448 14.4415 2.79999 16.0755 2.79999H39.4223C41.0562 2.79999 42.3807 4.12461 42.3807 5.75846V9.59609C42.3807 11.23 41.0563 12.5547 39.4223 12.5547H16.0755C14.4414 12.5547 13.1169 11.2301 13.1169 9.59609V5.75846Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M8.39999 17.433C8.39999 15.6372 9.85576 14.1815 11.6515 14.1815H44.1669C45.9627 14.1815 47.4184 15.6372 47.4184 17.433V49.9484C47.4184 51.7441 45.9627 53.1999 44.1669 53.1999H11.6515C9.85576 53.1999 8.39999 51.7441 8.39999 49.9484V17.433ZM44.1669 17.433H11.6515V49.9484H44.1669V17.433Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M14.8928 26.3749C14.8928 23.2323 17.4404 20.6847 20.583 20.6847C23.7256 20.6847 26.2732 23.2323 26.2732 26.3749C26.2732 29.5175 23.7256 32.0651 20.583 32.0651C17.4404 32.0651 14.8928 29.5175 14.8928 26.3749Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M14.8928 41.0068C14.8928 37.8642 17.4404 35.3166 20.583 35.3166C23.7256 35.3166 26.2732 37.8642 26.2732 41.0068C26.2732 44.1494 23.7256 46.697 20.583 46.697C17.4404 46.697 14.8928 44.1494 14.8928 41.0068Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M29.5247 26.3749C29.5247 23.2323 32.0723 20.6847 35.2149 20.6847C38.3575 20.6847 40.9051 23.2323 40.9051 26.3749C40.9051 29.5175 38.3575 32.0651 35.2149 32.0651C32.0723 32.0651 29.5247 29.5175 29.5247 26.3749Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M29.5247 41.0068C29.5247 37.8642 32.0723 35.3166 35.2149 35.3166C38.3575 35.3166 40.9051 37.8642 40.9051 41.0068C40.9051 44.1494 38.3575 46.697 35.2149 46.697C32.0723 46.697 29.5247 44.1494 29.5247 41.0068Z"
      fill={color}
    />
  </svg>
);

const Amp = ({ size = 40, color = GRAY }: Props) => (
  <svg width={size} height={size} viewBox="0 0 128 128" fill="none" aria-label="Amp">
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M17.76 39.68C13.2533 39.68 9.59998 43.3334 9.59998 47.8401V89.5467C9.59998 94.0534 13.2533 97.7067 17.76 97.7067H110.24C114.747 97.7067 118.4 94.0534 118.4 89.5467V47.8401C118.4 43.3334 114.747 39.68 110.24 39.68H17.76ZM22.2933 46.9334C19.2889 46.9334 16.8533 49.369 16.8533 52.3734V85.0134C16.8533 88.0178 19.2889 90.4534 22.2933 90.4534H105.707C108.711 90.4534 111.147 88.0178 111.147 85.0134V52.3734C111.147 49.369 108.711 46.9334 105.707 46.9334H22.2933Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M40.841 46.1686C40.841 46.1686 40.842 46.1679 38.6134 43.3067C36.3848 40.4456 36.386 40.4446 36.386 40.4446L36.3918 40.4401L36.4039 40.4307L36.4434 40.4003L36.5818 40.2947C36.7 40.2051 36.8695 40.0778 37.0862 39.9183C37.5195 39.5994 38.1429 39.1507 38.9237 38.6154C40.482 37.5472 42.6851 36.1221 45.2699 34.6929C50.2998 31.9117 57.326 28.8 64 28.8C70.6741 28.8 77.7002 31.9117 82.7302 34.6929C85.315 36.1221 87.5181 37.5472 89.0764 38.6154C89.8572 39.1507 90.4806 39.5994 90.9139 39.9183C91.1306 40.0778 91.3001 40.2051 91.4183 40.2947L91.5567 40.4003L91.5962 40.4307L91.6083 40.4401L91.6124 40.4434C91.6124 40.4434 91.6153 40.4456 89.3867 43.3067C87.1581 46.1679 87.1591 46.1686 87.1591 46.1686L87.1359 46.1507L87.0358 46.0744C86.9438 46.0046 86.8021 45.8981 86.6146 45.7601C86.2394 45.484 85.6823 45.0827 84.9752 44.598C83.5576 43.6262 81.555 42.3314 79.2205 41.0405C74.4118 38.3817 68.7446 36.0534 64 36.0534C59.2555 36.0534 53.5883 38.3817 48.7796 41.0405C46.445 42.3314 44.4425 43.6262 43.0249 44.598C42.3178 45.0827 41.7607 45.484 41.3855 45.7601C41.198 45.8981 41.0563 46.0046 40.9643 46.0744L40.8642 46.1507L40.841 46.1686Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M35.2 67.2C31.6654 67.2 28.8 70.0653 28.8 73.6V92.8H99.2V73.6C99.2 70.0653 96.3346 67.2 92.8 67.2H35.2ZM44.8 83.2C47.451 83.2 49.6 81.051 49.6 78.4C49.6 75.749 47.451 73.6 44.8 73.6C42.149 73.6 40 75.749 40 78.4C40 81.051 42.149 83.2 44.8 83.2ZM68.8 78.4C68.8 81.051 66.651 83.2 64 83.2C61.349 83.2 59.2 81.051 59.2 78.4C59.2 75.749 61.349 73.6 64 73.6C66.651 73.6 68.8 75.749 68.8 78.4ZM83.2 83.2C85.851 83.2 88 81.051 88 78.4C88 75.749 85.851 73.6 83.2 73.6C80.549 73.6 78.4 75.749 78.4 78.4C78.4 81.051 80.549 83.2 83.2 83.2Z"
      fill={color}
    />
  </svg>
);

const Pedal = ({ size = 40, color = GRAY }: Props) => (
  <svg width={size} height={size} viewBox="0 0 56 56" fill="none" aria-label="Pedal">
    {/* The web version wraps these in a full-rect clipPath — a no-op, dropped
        here to avoid duplicate SVG ids when several pedals render at once. */}
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M13.3489 11.5095C13.3489 8.24585 15.9946 5.60016 19.2582 5.60016H36.4489C39.7126 5.60016 42.3582 8.24585 42.3582 11.5095V45.8909C42.3582 49.1545 39.7126 51.8002 36.4489 51.8002H19.2582C15.9946 51.8002 13.3489 49.1545 13.3489 45.8909V11.5095ZM19.2582 8.82341C17.7748 8.82341 16.5722 10.026 16.5722 11.5095V45.8909C16.5722 47.3744 17.7748 48.5769 19.2582 48.5769H36.4489C37.9325 48.5769 39.135 47.3744 39.135 45.8909V11.5095C39.135 10.026 37.9325 8.82341 36.4489 8.82341H19.2582Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M13.3489 24.4024C13.3489 22.3256 15.0325 20.6419 17.1094 20.6419H38.5978C40.6746 20.6419 42.3582 22.3256 42.3582 24.4024V45.8908C42.3582 49.1544 39.7126 51.8001 36.4489 51.8001H19.2582C15.9946 51.8001 13.3489 49.1544 13.3489 45.8908V24.4024ZM17.1094 23.8652C16.8127 23.8652 16.5722 24.1057 16.5722 24.4024V45.8908C16.5722 47.3743 17.7748 48.5768 19.2582 48.5768H36.4489C37.9325 48.5768 39.135 47.3743 39.135 45.8908V24.4024C39.135 24.1057 38.8945 23.8652 38.5978 23.8652H17.1094Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M19.7954 35.1467C19.7954 34.2566 20.5169 33.535 21.407 33.535H34.3C35.1901 33.535 35.9117 34.2566 35.9117 35.1467V43.742C35.9117 44.6321 35.1901 45.3536 34.3 45.3536H21.407C20.5169 45.3536 19.7954 44.6321 19.7954 43.742V35.1467Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M39.1349 26.5513C39.1349 25.6613 39.8565 24.9397 40.7466 24.9397H42.8954C43.7854 24.9397 44.507 25.6613 44.507 26.5513V30.849C44.507 31.739 43.7854 32.4606 42.8954 32.4606H40.7466C39.8565 32.4606 39.1349 31.739 39.1349 30.849V26.5513Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M11.2 26.5513C11.2 25.6613 11.9216 24.9397 12.8117 24.9397H14.9605C15.8506 24.9397 16.5721 25.6613 16.5721 26.5513V30.849C16.5721 31.739 15.8506 32.4606 14.9605 32.4606H12.8117C11.9216 32.4606 11.2 31.739 11.2 30.849V26.5513Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M33.2256 10.9722C31.1488 10.9722 29.4652 12.6558 29.4652 14.7326C29.4652 16.8095 31.1488 18.4931 33.2256 18.4931C35.3025 18.4931 36.9861 16.8095 36.9861 14.7326C36.9861 12.6558 35.3025 10.9722 33.2256 10.9722Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M22.4814 10.9722C20.4046 10.9722 18.7209 12.6558 18.7209 14.7326C18.7209 16.8095 20.4046 18.4931 22.4814 18.4931C24.5583 18.4931 26.2419 16.8095 26.2419 14.7326C26.2419 12.6558 24.5583 10.9722 22.4814 10.9722Z"
      fill={color}
    />
  </svg>
);

const Outboard = ({ size = 40, color = GRAY }: Props) => (
  <svg width={size} height={size} viewBox="0 0 40 40" fill="none" aria-label="Outboard">
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M5.6 15.3999C4.93722 15.3999 4.4 15.9372 4.4 16.5999V23.7999C4.4 24.4626 4.93726 24.9999 5.6 24.9999H34.4C35.0627 24.9999 35.6 24.4626 35.6 23.7999V16.5999C35.6 15.9372 35.0628 15.3999 34.4 15.3999H5.6ZM2 16.5999C2 14.6117 3.61174 12.9999 5.6 12.9999H34.4C36.3883 12.9999 38 14.6117 38 16.5999V23.7999C38 25.788 36.3883 27.3999 34.4 27.3999H5.6C3.6117 27.3999 2 25.788 2 23.7999V16.5999Z"
      fill={color}
    />
    <path
      d="M9.19932 22.6007C10.5248 22.6007 11.5993 21.5262 11.5993 20.2007C11.5993 18.8752 10.5248 17.8007 9.19932 17.8007C7.87383 17.8007 6.79932 18.8752 6.79932 20.2007C6.79932 21.5262 7.87383 22.6007 9.19932 22.6007Z"
      fill={color}
    />
    <path
      d="M16.3994 22.6007C17.7249 22.6007 18.7994 21.5262 18.7994 20.2007C18.7994 18.8752 17.7249 17.8007 16.3994 17.8007C15.0739 17.8007 13.9994 18.8752 13.9994 20.2007C13.9994 21.5262 15.0739 22.6007 16.3994 22.6007Z"
      fill={color}
    />
    <path
      d="M30.7994 17.8007H24.7994C23.4739 17.8007 22.3994 18.8752 22.3994 20.2007C22.3994 21.5262 23.4739 22.6007 24.7994 22.6007H30.7994C32.1249 22.6007 33.1994 21.5262 33.1994 20.2007C33.1994 18.8752 32.1249 17.8007 30.7994 17.8007Z"
      fill={color}
    />
  </svg>
);

const SpeakerCab = ({ size = 40, color = GRAY }: Props) => (
  <svg width={size} height={size} viewBox="0 0 20 20" fill="none" aria-label="Cabinet">
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M3 4.16667C3 3.52233 3.52233 3 4.16667 3H15.8333C16.4777 3 17 3.52233 17 4.16667V15.8333C17 16.4777 16.4777 17 15.8333 17H4.16667C3.52233 17 3 16.4777 3 15.8333V4.16667ZM15.8333 4.16667H4.16667V15.8333H15.8333V4.16667Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M5.32983 7.37501C5.32983 6.24743 6.24392 5.33334 7.3715 5.33334C8.49908 5.33334 9.41317 6.24743 9.41317 7.37501C9.41317 8.50259 8.49908 9.41668 7.3715 9.41668C6.24392 9.41668 5.32983 8.50259 5.32983 7.37501Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M5.32983 12.625C5.32983 11.4974 6.24392 10.5833 7.3715 10.5833C8.49908 10.5833 9.41317 11.4974 9.41317 12.625C9.41317 13.7526 8.49908 14.6667 7.3715 14.6667C6.24392 14.6667 5.32983 13.7526 5.32983 12.625Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M10.5798 7.37501C10.5798 6.24743 11.4939 5.33334 12.6215 5.33334C13.7491 5.33334 14.6632 6.24743 14.6632 7.37501C14.6632 8.50259 13.7491 9.41668 12.6215 9.41668C11.4939 9.41668 10.5798 8.50259 10.5798 7.37501Z"
      fill={color}
    />
    <path
      fillRule="evenodd"
      clipRule="evenodd"
      d="M10.5798 12.625C10.5798 11.4974 11.4939 10.5833 12.6215 10.5833C13.7491 10.5833 14.6632 11.4974 14.6632 12.625C14.6632 13.7526 13.7491 14.6667 12.6215 14.6667C11.4939 14.6667 10.5798 13.7526 10.5798 12.625Z"
      fill={color}
    />
  </svg>
);

const Space = ({ size = 40, color = GRAY }: Props) => (
  <svg width={size} height={size} viewBox="0 0 20 20" fill="none" aria-label="Spaces">
    <path
      d="M9.23661 15.7115H2.65918"
      stroke={color}
      strokeWidth="1.2"
      strokeLinecap="round"
      strokeLinejoin="round"
    />
    <path
      d="M9.23657 4.42899V16.2369C9.2366 16.3479 9.26192 16.4575 9.31061 16.5573C9.3593 16.657 9.43007 16.7444 9.51756 16.8127C9.60505 16.881 9.70696 16.9285 9.81554 16.9516C9.92413 16.9747 10.0365 16.9727 10.1443 16.9458L15.0832 15.7115V5.15982C15.0831 4.83387 14.9741 4.5173 14.7735 4.26041C14.5729 4.00352 14.2922 3.82105 13.976 3.74201L11.0527 3.01119C10.8373 2.95734 10.6124 2.95328 10.3952 2.99932C10.178 3.04535 9.97411 3.14026 9.79906 3.27685C9.62401 3.41344 9.48237 3.58812 9.38491 3.78763C9.28745 3.98713 9.23672 4.20695 9.23657 4.42899Z"
      stroke={color}
      strokeWidth="1.2"
      strokeLinecap="round"
      strokeLinejoin="round"
    />
    <path
      d="M9.23665 4.01825H7.04417C6.65652 4.01825 6.28474 4.17224 6.01063 4.44636C5.73651 4.72047 5.58252 5.09225 5.58252 5.4799V15.7115"
      stroke={color}
      strokeWidth="1.2"
      strokeLinecap="round"
      strokeLinejoin="round"
    />
    <path
      d="M11.429 9.86487H11.4373"
      stroke={color}
      strokeWidth="1.2"
      strokeLinecap="round"
      strokeLinejoin="round"
    />
    <path
      d="M17.2757 15.7115H15.0833"
      stroke={color}
      strokeWidth="1.2"
      strokeLinecap="round"
      strokeLinejoin="round"
    />
  </svg>
);

const Experimental = ({ size = 40, color = GRAY }: Props) => (
  <svg width={size} height={size} viewBox="0 0 20 20" fill="none" aria-label="Experimental">
    <path
      d="M10 11C10.5523 11 11 10.5523 11 10C11 9.44772 10.5523 9 10 9C9.44772 9 9 9.44772 9 10C9 10.5523 9.44772 11 10 11Z"
      fill={color}
    />
    <path
      d="M15.4369 15.437C16.7976 14.083 15.4503 10.5278 12.4354 7.49966C9.40725 4.48481 5.85213 3.13747 4.49812 4.49815C3.13744 5.85216 4.48478 9.40728 7.49963 12.4355C10.5278 15.4503 14.0829 16.7976 15.4369 15.437Z"
      stroke={color}
      strokeWidth="1.2"
      strokeLinecap="round"
      strokeLinejoin="round"
    />
    <path
      d="M12.4354 12.4355C15.4503 9.40728 16.7976 5.85216 15.4369 4.49815C14.0829 3.13747 10.5278 4.48481 7.49963 7.49966C4.48478 10.5278 3.13744 14.083 4.49812 15.437C5.85213 16.7976 9.40725 15.4503 12.4354 12.4355Z"
      stroke={color}
      strokeWidth="1.2"
      strokeLinecap="round"
      strokeLinejoin="round"
    />
  </svg>
);

const Ir = ({ size = 40, color = GRAY }: Props) => (
  <svg width={size} height={size} viewBox="0 0 40 40" fill="none" aria-label="Impulse Response">
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
 * web's icon background (#151517 = SURFACE), sized ~40% of the box like the
 * web's ToneCard/ToneImage fallbacks. Fills its parent; pass the box size so
 * the glyph scales with the card/tile.
 */
export const GearImageFallback: React.FC<{ gear?: string; boxSize: number }> = ({
  gear,
  boxSize,
}) => (
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
    <GearIcon gear={gear} size={Math.round(boxSize * 0.4)} />
  </div>
);

/**
 * Tone artwork with recovery: renders the image URL when present and swaps in
 * the gear-glyph fallback if it's missing or the network fetch fails (offline
 * / tone3000.com down). Fills its parent like a plain cover <img>.
 */
export const ToneImage: React.FC<{
  src: string | undefined;
  alt: string;
  gear?: string;
  boxSize: number;
  draggable?: boolean;
}> = ({ src, alt, gear, boxSize, draggable }) => {
  const [failed, setFailed] = React.useState(false);
  // A new URL (tone swap/model switch) gets a fresh chance to load.
  React.useEffect(() => setFailed(false), [src]);

  if (!src || failed) return <GearImageFallback gear={gear} boxSize={boxSize} />;
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
