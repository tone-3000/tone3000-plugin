// Glyphs the web UI drew as inline SVG because Lucide has no equivalent.
// Authored white (stroke/fill) like the Lucide set so Icons::draw can tint.
#pragma once

namespace t3k::ui::custom_icons {

// Tuning fork in Lucide's 24x24 stroke style (PluginHeader.tsx).
inline constexpr const char* kTuningFork =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M8 3v7a4 4 0 0 0 8 0V3"/><line x1="12" y1="14" x2="12" y2="21"/></svg>)svg";

// Two overlapping 12px circles for the stereo segment (StereoModeToggle.tsx,
// size 12: r = 6, overlap = 5.4).
inline constexpr const char* kStereoCircles =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 17.4 12" fill="none" stroke="#ffffff" stroke-width="1.5" stroke-linecap="round"><circle cx="6" cy="6" r="5"/><circle cx="11.4" cy="6" r="5"/></svg>)svg";

// The faceplate input-mode glyph (Faceplate.tsx StereoIcon): two overlapping
// circles in a 17x10 box, stroke 1.25.
inline constexpr const char* kInputStereo =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 17 10" fill="none" stroke="#ffffff" stroke-width="1.25"><circle cx="5" cy="5" r="4.375"/><circle cx="11.6665" cy="5" r="4.375"/></svg>)svg";

// Lucide Circle at strokeWidth 3 (the mono segment).
inline constexpr const char* kCircleBold =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/></svg>)svg";

// Web navbar hamburger (AccountMenu.tsx; Lucide Menu is too tall for the pill).
inline constexpr const char* kHamburger =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"><path d="M4 6H20" stroke="#ffffff" stroke-width="2" stroke-linecap="round"/><path d="M4 12H20" stroke="#ffffff" stroke-width="2" stroke-linecap="round"/><path d="M4 18H20" stroke="#ffffff" stroke-width="2" stroke-linecap="round"/></svg>)svg";

// Lucide Bookmark with `fill` (the favorited state of the detail card's
// bookmark tally).
inline constexpr const char* kBookmarkFilled =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#ffffff" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17 3a2 2 0 0 1 2 2v15a1 1 0 0 1-1.496.868l-4.512-2.578a2 2 0 0 0-1.984 0l-4.512 2.578A1 1 0 0 1 5 20V5a2 2 0 0 1 2-2z"/></svg>)svg";

// EQ view switcher glyphs (ChainBlock.tsx EqSlidersIcon / EqCurveIcon):
// 16-unit boxes, stroke 1.333 / 1.5.
inline constexpr const char* kEqSliders =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="none" stroke="#ffffff" stroke-width="1.33333" stroke-linecap="round" stroke-linejoin="round"><path d="M11.3333 6.66669V12.6667"/><path d="M4.66675 3.33331V9.33331"/><path d="M13.3333 4.66669C13.3333 3.56212 12.4378 2.66669 11.3333 2.66669C10.2287 2.66669 9.33325 3.56212 9.33325 4.66669C9.33325 5.77126 10.2287 6.66669 11.3333 6.66669C12.4378 6.66669 13.3333 5.77126 13.3333 4.66669Z"/><path d="M6.66675 11.3333C6.66675 10.2287 5.77132 9.33331 4.66675 9.33331C3.56218 9.33331 2.66675 10.2287 2.66675 11.3333C2.66675 12.4379 3.56218 13.3333 4.66675 13.3333C5.77132 13.3333 6.66675 12.4379 6.66675 11.3333Z"/></svg>)svg";
inline constexpr const char* kEqCurve =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="none" stroke="#ffffff" stroke-width="1.5" stroke-linecap="round"><path d="M1 13.5C5 13.5 5.5 2.5 8 2.5C10.5 2.5 11 13.5 15 13.5"/></svg>)svg";

// Hardcase / interface glyph for the AUDIO INTERFACE settings group
// (SystemSettings.tsx AudioInterfaceIcon): 20-unit box, stroke 1.667.
inline constexpr const char* kAudioInterface =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="none" stroke="#ffffff" stroke-width="1.66667" stroke-linecap="round" stroke-linejoin="round"><path d="M8.33398 13.3333H8.34232"/><path d="M1.84268 9.64751C1.72652 9.87927 1.66603 10.1349 1.66602 10.3942V15C1.66602 15.442 1.84161 15.866 2.15417 16.1785C2.46673 16.4911 2.89065 16.6667 3.33268 16.6667H16.666C17.108 16.6667 17.532 16.4911 17.8445 16.1785C18.1571 15.866 18.3327 15.442 18.3327 15V10.3942C18.3327 10.1349 18.2722 9.87927 18.156 9.64751L15.4577 4.25834C15.3197 3.98067 15.107 3.74699 14.8435 3.58358C14.58 3.42017 14.2761 3.33351 13.966 3.33334H6.03268C5.72261 3.33351 5.41874 3.42017 5.15522 3.58358C4.8917 3.74699 4.679 3.98067 4.54102 4.25834L1.84268 9.64751Z"/><path d="M18.2876 10.0108H1.71094"/><path d="M5 13.3333H5.00833"/></svg>)svg";

// Placeholder avatar (person glyph) for creators without an avatar_url
// (AvatarFallback.tsx). Tint with theme::kGray.
inline constexpr const char* kAvatarFallback =
    R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 224 224" fill="none"><path d="M112 224C96.7007 224 82.2797 221.072 68.7373 215.216C55.268 209.36 43.3725 201.271 33.051 190.949C22.7294 180.628 14.6405 168.732 8.78431 155.263C2.9281 141.72 0 127.299 0 112C0 96.7008 2.9281 82.3165 8.78431 68.8472C14.6405 55.3047 22.6928 43.3727 32.9412 33.0511C43.2627 22.7295 55.1582 14.6406 68.6274 8.78442C82.1699 2.92821 96.5908 0.000106812 111.89 0.000106812C127.19 0.000106812 141.61 2.92821 155.153 8.78442C168.695 14.6406 180.627 22.7295 190.949 33.0511C201.271 43.3727 209.359 55.3047 215.216 68.8472C221.072 82.3165 224 96.7008 224 112C224 127.299 221.072 141.72 215.216 155.263C209.359 168.732 201.271 180.628 190.949 190.949C180.627 201.271 168.695 209.36 155.153 215.216C141.684 221.072 127.299 224 112 224ZM112 207.31C120.345 207.31 128.617 206.175 136.816 203.906C145.088 201.71 152.847 198.489 160.094 194.243C167.341 190.071 173.783 185.02 179.42 179.09C175.467 172.795 170.05 167.451 163.169 163.059C156.361 158.594 148.565 155.226 139.78 152.957C131.069 150.614 121.809 149.443 112 149.443C102.044 149.443 92.6745 150.614 83.8902 152.957C75.1059 155.299 67.3098 158.703 60.502 163.169C53.7673 167.561 48.4235 172.868 44.4706 179.09C50.1072 185.02 56.549 190.071 63.7961 194.243C71.1163 198.489 78.8758 201.71 87.0745 203.906C95.2732 206.175 103.582 207.31 112 207.31ZM112 130.777C119.027 130.85 125.359 129.093 130.996 125.506C136.706 121.846 141.244 116.868 144.612 110.573C147.979 104.277 149.663 97.2132 149.663 89.3805C149.663 81.987 147.979 75.2158 144.612 69.0668C141.244 62.9178 136.706 58.0132 130.996 54.353C125.286 50.6197 118.954 48.753 112 48.753C104.973 48.753 98.6039 50.6197 92.8941 54.353C87.1843 58.0132 82.6457 62.9178 79.2784 69.0668C75.9111 75.2158 74.264 81.987 74.3372 89.3805C74.3372 97.2132 75.9843 104.241 79.2784 110.463C82.6457 116.685 87.1477 121.626 92.7843 125.286C98.4941 128.873 104.899 130.703 112 130.777Z" fill="#ffffff"/></svg>)svg";

}  // namespace t3k::ui::custom_icons
