TONE3000 for Linux
==================

Install (recommended)
---------------------
  ./install.sh

This installs the VST3 to ~/.vst3, the LV2 to ~/.lv2, the CLAP to ~/.clap
and the standalone app to ~/.local/bin (with a desktop entry and icon so it
shows up in your applications menu), checking for required system libraries
first and offering to install any that are missing. Factory presets land in
~/.config/TONE3000/Presets/Factory.

  ./install.sh --check       check dependencies only, install nothing
  ./install.sh --uninstall   remove a previous install

Manual install
--------------
Copy TONE3000.vst3 to ~/.vst3/, TONE3000.lv2 to ~/.lv2/, TONE3000.clap to
~/.clap/ and (optionally) the TONE3000 binary anywhere on your PATH. Install
only the formats your DAW uses; one is enough.

Runtime dependencies
--------------------
Required: libcurl (tone downloads), ALSA, FreeType. Present on almost
every desktop install; `./install.sh --check` verifies and offers to
install anything missing.

  Ubuntu / Debian:  sudo apt install libcurl4 libasound2 libfreetype6
  Fedora:           sudo dnf install libcurl alsa-lib freetype
  Arch:             sudo pacman -S curl alsa-lib freetype2
  openSUSE:         sudo zypper install libcurl4 alsa libfreetype6

Troubleshooting
---------------
Log file: ~/.config/TONE3000/TONE3000.log
Unresolved libraries: ldd ./TONE3000 | grep "not found"
