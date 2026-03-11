#!/bin/bash

# TONE3000 AU Plugin Copy Script
# Copies the AU plugin to the correct directory on macOS

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Ensure macOS
ensure_macos() {
    local os="$(uname -s)"
    if [[ "$os" != "Darwin" ]]; then
        print_error "This script only supports macOS for AU installation. Detected: $os"
        exit 1
    fi
}

# AU install destinations
get_au_dirs() {
    echo "$HOME/Library/Audio/Plug-Ins/Components /Library/Audio/Plug-Ins/Components"
}

validate_build_type() {
    local build_type=$1
    case $build_type in
        "Debug"|"Release") return 0 ;;
        *) return 1 ;;
    esac
}

copy_au() {
    local build_type=$1
    ensure_macos
    local au_dirs=$(get_au_dirs)

    # JUCE 8 default artefacts folder layout
    local source_dir="build/plugin/TONE3000_artefacts/$build_type/AU"
    local plugin_name="TONE3000.component"
    local source_path="$source_dir/$plugin_name"

    print_status "Build type: $build_type"
    print_status "Source: $source_path"

    if [ ! -d "$source_path" ]; then
        print_error "AU plugin not found at: $source_path"
        print_error "Make sure you have built with AU enabled, e.g.:"
        print_error "  cmake -B build -S . -DHEADLESS=OFF -DCMAKE_BUILD_TYPE=$build_type"
        print_error "  cmake --build build"
        exit 1
    fi

    for au_dir in $au_dirs; do
        local dest_path="$au_dir/$plugin_name"
        print_status "Installing to: $dest_path"

        if [ ! -d "$au_dir" ]; then
            print_status "Creating AU directory: $au_dir"
            if [[ "$au_dir" == "/Library/Audio/Plug-Ins/Components" ]]; then
                sudo mkdir -p "$au_dir"
            else
                mkdir -p "$au_dir"
            fi
        fi

        if [ -d "$dest_path" ]; then
            print_warning "Removing existing plugin at: $dest_path"
            if [[ "$au_dir" == "/Library/Audio/Plug-Ins/Components" ]]; then
                sudo rm -rf "$dest_path"
            else
                rm -rf "$dest_path"
            fi
        fi

        print_status "Copying AU plugin..."
        if [[ "$au_dir" == "/Library/Audio/Plug-Ins/Components" ]]; then
            sudo cp -r "$source_path" "$dest_path"
        else
            cp -r "$source_path" "$dest_path"
        fi

        if [ $? -eq 0 ]; then
            print_success "AU plugin copied successfully to: $dest_path"
        else
            print_error "Failed to copy AU plugin to: $dest_path"
            exit 1
        fi
    done

    print_status "If Logic/DAW is open, restart or rescan Audio Units. You may need to run 'auval -a' or clear AU caches."
}

main() {
    local build_type=${1:-"Release"}

    print_status "TONE3000 AU Plugin Copy Script"
    print_status "=================================="

    if ! validate_build_type "$build_type"; then
        print_error "Invalid build type: $build_type"
        print_error "Valid options: Debug, Release"
        echo "Usage: $0 [Debug|Release]"
        exit 1
    fi

    copy_au "$build_type"
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi