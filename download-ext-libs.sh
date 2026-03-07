#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXT_LIBS_DIR="$SCRIPT_DIR/ext_libs"

# OpenVINO storage base URLs
OPENVINO_STORAGE="https://storage.openvinotoolkit.org/repositories"
FILETREE_URL="https://storage.openvinotoolkit.org/filetree.json"
OS_PLATFORM="ubuntu24"
ARCH="x86_64"

echo "=== OpenVINO External Libraries Downloader ==="
echo "Downloading prebuilt binaries from storage.openvinotoolkit.org"
echo

# Function to discover latest version from filetree.json
get_latest_storage_version() {
    local repo_name="$1"
    
    python3 - "$repo_name" "$FILETREE_URL" <<'PY'
import json
import re
import sys
import urllib.request

repo_name = sys.argv[1]
filetree_url = sys.argv[2]

def find_packages(node, target_repo):
    """Find packages directory for target repository"""
    if node.get('name') == target_repo and node.get('type') == 'directory':
        if 'children' in node:
            for child in node['children']:
                if child['name'] == 'packages' and child.get('type') == 'directory':
                    return child.get('children', [])
    
    if node.get('type') == 'directory' and 'children' in node:
        for child in node['children']:
            result = find_packages(child, target_repo)
            if result:
                return result
    return None

try:
    req = urllib.request.Request(filetree_url)
    with urllib.request.urlopen(req) as resp:
        data = json.load(resp)
    
    packages = find_packages(data, repo_name)
    
    if packages:
        # Filter version directories (exclude pre-release, weekly, etc.)
        versions = []
        for pkg in packages:
            if pkg.get('type') == 'directory':
                name = pkg['name']
                # Match version patterns like 2025.4, 2025.4.0, 2025.4.1.0
                if re.match(r'^\d+\.\d+', name) and 'pre-release' not in name and 'weekly' not in name:
                    versions.append(name)
        
        if versions:
            # Sort versions numerically
            versions.sort(key=lambda v: [int(x) for x in re.findall(r'\d+', v)])
            print(versions[-1])
        else:
            sys.exit(1)
    else:
        sys.exit(1)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)
PY
}

echo "Step 1: Discovering latest GenAI version..."
GENAI_VERSION=$(get_latest_storage_version "openvino_genai")
if [ -z "$GENAI_VERSION" ]; then
    echo "Error: Could not determine latest GenAI version"
    exit 1
fi
echo "  Found GenAI version: $GENAI_VERSION"
echo "  (includes OpenVINO runtime, tokenizers, and all plugins)"

echo
echo "Step 2: Building download URL..."

echo "  Discovering GenAI package filename..."
GENAI_URL=$(python3 - "$GENAI_VERSION" "$OS_PLATFORM" "$ARCH" "$FILETREE_URL" <<'PY'
import json
import sys
import urllib.request

version, platform, arch, filetree_url = sys.argv[1:5]

def find_file(node, path_parts, platform, arch):
    """Navigate filetree to find matching file"""
    if not path_parts:
        # We're at the target directory, find the file
        if node.get('type') == 'directory' and 'children' in node:
            for child in node['children']:
                if child.get('type') == 'file':
                    name = child['name']
                    if name.endswith('.tar.gz') and platform in name and arch in name:
                        return name
        return None
    
    # Navigate deeper
    target = path_parts[0]
    if node.get('type') == 'directory' and 'children' in node:
        for child in node['children']:
            if child['name'] == target:
                return find_file(child, path_parts[1:], platform, arch)
    return None

try:
    req = urllib.request.Request(filetree_url)
    with urllib.request.urlopen(req) as resp:
        data = json.load(resp)
    
    # Root is already 'production', so path starts from 'repositories'
    path = ['repositories', 'openvino_genai', 'packages', version, 'linux']
    filename = find_file(data, path, platform, arch)
    
    if filename:
        base_url = f"https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/{version}/linux/"
        print(f"{base_url}{filename}")
    else:
        sys.exit(1)
except Exception as e:
    print(f"Error: {e}", file=sys.stderr)
    sys.exit(1)
PY
)

if [ -z "$GENAI_URL" ]; then
    echo "Error: Could not find GenAI package"
    exit 1
fi
echo "    $GENAI_URL"

echo
echo "Step 3: Downloading package..."
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

echo "  Downloading GenAI (includes all dependencies)..."
curl -L --progress-bar -o "$TEMP_DIR/genai.tar.gz" "$GENAI_URL"

echo
echo "Step 4: Extracting package to $EXT_LIBS_DIR..."

# Create ext_libs directory if it doesn't exist
mkdir -p "$EXT_LIBS_DIR"

# Extract GenAI (contains runtime/ with all libraries)
echo "  Extracting GenAI..."
rm -rf "$EXT_LIBS_DIR/genai-src"
mkdir -p "$EXT_LIBS_DIR/genai-src"
tar -xzf "$TEMP_DIR/genai.tar.gz" -C "$TEMP_DIR"
GENAI_EXTRACTED=$(find "$TEMP_DIR" -maxdepth 1 -type d -name "*genai*" | head -n 1)
if [ -n "$GENAI_EXTRACTED" ]; then
    cp -r "$GENAI_EXTRACTED"/* "$EXT_LIBS_DIR/genai-src/"
    echo "    Extracted to: $EXT_LIBS_DIR/genai-src"
    
    # Verify the runtime structure
    if [ -d "$EXT_LIBS_DIR/genai-src/runtime/lib/intel64" ]; then
        echo "    ✓ Found runtime libraries in genai-src/runtime/lib/intel64"
        echo "      - libopenvino.so (OpenVINO runtime)"
        echo "      - libopenvino_genai.so (GenAI)"
        echo "      - libopenvino_tokenizers.so (Tokenizers)"
        echo "      - libopenvino_intel_cpu_plugin.so (CPU plugin)"
        echo "      - libopenvino_intel_gpu_plugin.so (GPU plugin)"
    else
        echo "    Warning: Expected runtime structure not found"
    fi
else
    echo "    Warning: GenAI extraction structure unexpected, trying direct extraction"
    tar -xzf "$TEMP_DIR/genai.tar.gz" -C "$EXT_LIBS_DIR/genai-src" --strip-components=1
fi

echo
echo "=== Download and extraction complete ==="
echo "Version installed:"
echo "  GenAI: $GENAI_VERSION (includes all dependencies)"
echo
echo "Next steps:"
echo "  1. Review the extracted contents in $EXT_LIBS_DIR/genai-src/runtime"
echo "  2. Run ./build.sh to build the ov-prompter binary"
