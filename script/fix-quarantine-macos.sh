#!/usr/bin/env bash
# Remove macOS quarantine from CI-built artifacts.

xattr -cr Downloads/TONE3000-macOS\ ARM64-Release
