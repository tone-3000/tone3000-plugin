#!/bin/bash

# TONE3000 VST3 Plugin Copy Script
# Copies the VST3 plugin to the correct directory based on OS and build type

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
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

# Function to detect OS
detect_os() {
    case "$(uname -s)" in
        Darwin*)    echo "macos";;
        Linux*)     echo "linux";;
        CYGWIN*|MINGW32*|MSYS*|MINGW*) echo "windows";;
        *)          echo "unknown";;
    esac
}

# Function to get VST3 directories based on OS
get_vst3_dirs() {
    local os=$1
    case $os in
        "macos")
            echo "$HOME/Library/Audio/Plug-Ins/VST3 /Library/Audio/Plug-Ins/VST3"
            ;;
        "linux")
            echo "$HOME/.vst3"
            ;;
        "windows")
            echo "C:/Program Files/Common Files/VST3"
            ;;
        *)
            print_error "Unsupported OS: $os"
            exit 1
            ;;
    esac
}

# Function to check if build type is valid
validate_build_type() {
    local build_type=$1
    case $build_type in
        "Debug"|"Release")
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

# Function to copy VST3 plugin
copy_vst3() {
    local build_type=$1
    local os=$(detect_os)
    local vst3_dirs=$(get_vst3_dirs "$os")
    local source_dir="build/plugin/TONE3000_artefacts/$build_type/VST3"
    local plugin_name="TONE3000.vst3"
    local source_path="$source_dir/$plugin_name"

    print_status "Detected OS: $os"
    print_status "Build type: $build_type"
    print_status "Source: $source_path"

    # Check if source exists
    if [ ! -d "$source_path" ]; then
        print_error "VST3 plugin not found at: $source_path"
        print_error "Make sure you have built the plugin first with:"
        print_error "  cmake -B build -S . -DHEADLESS=OFF -DCMAKE_BUILD_TYPE=$build_type"
        print_error "  cmake --build build"
        exit 1
    fi

    # Copy to each VST3 directory
    for vst3_dir in $vst3_dirs; do
        local dest_path="$vst3_dir/$plugin_name"
        
        print_status "Installing to: $dest_path"
        
        # Create destination directory if it doesn't exist
        if [ ! -d "$vst3_dir" ]; then
            print_status "Creating VST3 directory: $vst3_dir"
            if [[ "$vst3_dir" == "/Library/Audio/Plug-Ins/VST3" ]]; then
                # System directory requires sudo
                sudo mkdir -p "$vst3_dir"
            else
                mkdir -p "$vst3_dir"
            fi
        fi

        # Remove existing plugin if it exists
        if [ -d "$dest_path" ]; then
            print_warning "Removing existing plugin at: $dest_path"
            if [[ "$vst3_dir" == "/Library/Audio/Plug-Ins/VST3" ]]; then
                sudo rm -rf "$dest_path"
            else
                rm -rf "$dest_path"
            fi
        fi

        # Copy the plugin
        print_status "Copying VST3 plugin..."
        if [[ "$vst3_dir" == "/Library/Audio/Plug-Ins/VST3" ]]; then
            # System directory requires sudo
            sudo cp -r "$source_path" "$dest_path"
        else
            cp -r "$source_path" "$dest_path"
        fi

        if [ $? -eq 0 ]; then
            print_success "VST3 plugin copied successfully to: $dest_path"
        else
            print_error "Failed to copy VST3 plugin to: $dest_path"
            exit 1
        fi
    done

    print_status "Remember to rescan your plugins in your DAW's settings."
}

# Main script
main() {
    local build_type=${1:-"Release"}  # Default to Release if not specified

    print_status "TONE3000 VST3 Plugin Copy Script"
    print_status "=================================="

    # Validate build type
    if ! validate_build_type "$build_type"; then
        print_error "Invalid build type: $build_type"
        print_error "Valid options: Debug, Release"
        echo "Usage: $0 [Debug|Release]"
        exit 1
    fi

    # Copy the VST3 plugin
    copy_vst3 "$build_type"
}

# Check if script is being sourced or executed
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
