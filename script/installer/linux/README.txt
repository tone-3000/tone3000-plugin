TONE3000 for Linux
==================

Install (recommended)
---------------------
  ./install.sh

This installs the VST3 to ~/.vst3 and the standalone app to ~/.local/bin,
checking for required system libraries first and offering to install any
that are missing.

  ./install.sh --check       check dependencies only, install nothing
  ./install.sh --uninstall   remove a previous install

Manual install
--------------
Copy TONE3000.vst3 to ~/.vst3/ and (optionally) the TONE3000 binary
anywhere on your PATH.

Runtime dependencies
--------------------
The plugin UI renders in the system WebKitGTK webview. If it is not
installed, the plugin window will show a BLACK SCREEN.

Required: WebKitGTK 4.1 (or 4.0), GTK3, ALSA, FreeType.

  Ubuntu / Debian:  sudo apt install libwebkit2gtk-4.1-0
  Fedora:           sudo dnf install webkit2gtk4.1
  Arch:             sudo pacman -S webkit2gtk-4.1
  openSUSE:         sudo zypper install libwebkit2gtk-4_1-0

Troubleshooting
---------------
Log file: ~/.config/TONE3000/TONE3000.log
Unresolved libraries: ldd ./TONE3000 | grep "not found"
