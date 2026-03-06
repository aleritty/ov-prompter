# OV Prompter

[![GitHub Release](https://img.shields.io/github/v/release/aleritty/ov-prompter?logo=github&label=Releases)](https://github.com/aleritty/ov-prompter/releases)
[![Build & Release](https://github.com/aleritty/ov-prompter/actions/workflows/release.yml/badge.svg)](https://github.com/aleritty/ov-prompter/actions/workflows/release.yml)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)](https://github.com/aleritty/ov-prompter)
[![OpenVINO](https://img.shields.io/badge/OpenVINO-2026.0-purple.svg)](https://github.com/openvinotoolkit/openvino)
[![Made with C++](https://img.shields.io/badge/Made%20with-C%2B%2B-00599C.svg?logo=c%2B%2B)](https://isocpp.org/)

**OV Prompter** is a self-contained CLI tool that uses local OpenVINO© LLMs to generate precise bash commands from natural language questions. Get instant, context-aware shell commands without leaving your terminal—faster than searching Stack Overflow.

## Features

- 🚀 **Fast inference**: Generate commands in ~12s on modern Intel hardware (tested on Core Ultra 9 285H with Arc Pro 130T GPU, Phi-4-mini-instruct-int8-ov model)
- 🎯 **Precise output**: Returns executable bash one-liners with optional summaries and explanations
- 💻 **Multi-device support**: CPU, GPU (Intel Arc/Iris), and NPU acceleration
- 🔒 **Privacy-first**: Runs entirely offline with local models—no data leaves your machine
- ⚡ **Zero dependencies**: Bundles all runtime libraries; no system OpenVINO installation required
- 🎨 **Configurable verbosity**: From terse commands to detailed explanations with pitfalls

> **Note:** Development used vibeCoding assistance for code generation and refactoring.

## Safety & responsibility

- All commands suggested by ov-prompter must be reviewed and verified by **you** before execution. You bear sole responsibility for any actions taken in your shell.
- The authors disclaim any liability arising from running generated commands.
- The program never executes commands automatically; it only prints suggestions for you to run (or correct, or ignore) manually.

## Requirements

| Component    | Requirement                                                                      |
|--------------|----------------------------------------------------------------------------------|
| **OS**       | Linux (tested on Ubuntu 22.04+, Intel-based systems)                             |
| **Runtime**  | No system dependencies—all libraries bundled in release archives                 |
| **Build**    | CMake ≥ 3.23, C++17 compiler (GCC ≥ 12), Python 3 (for downloading dependencies) |
| **Hardware** | CPU (any), GPU (Intel Arc/Iris with level-zero drivers), or NPU                  |
| **Model**    | OpenVINO©-compatible LLM (GGUF/IR format, e.g., Phi-4-mini, TinyLlama)           |

## Quick Start

### 1. Install

Download the latest prebuilt release from the [Releases page](https://github.com/aleritty/ov-prompter/releases), extract it, and add to your PATH:

```bash
tar xzf ov-prompter-*.tar.gz
sudo mv ov-prompter /usr/local/bin/
```

Optional: add a convenient alias to your shell RC file (`~/.bashrc` or `~/.zshrc`):

```bash
alias AI='ov-prompter --question'
```

### 2. Download a model

Choose a quantized OpenVINO© model from Hugging Face:

#### Option A: Using git+lfs

```bash
git lfs install
git clone https://huggingface.co/OpenVINO/Phi-4-mini-instruct-int8-ov models/Phi-4-mini
```

#### Option B: Using Hugging Face CLI**

```bash
pip install huggingface_hub
hf download OpenVINO/Phi-4-mini-instruct-int8-ov --local-dir models/Phi-4-mini
```

**Recommended models:**

- `OpenVINO/Phi-4-mini-instruct-int8-ov` (best quality, ~2GB)
- `OpenVINO/TinyLlama-1.1B-Chat-v1.0-int8-ov` (fastest, ~600MB)

### 3. Configure defaults (optional)

```bash
ov-prompter --set-model-default ./path/to/models/Phi-4-mini
ov-prompter --set-device-default GPU,CPU
```

### 4. Start asking questions

```bash
ov-prompter --question "Find all Python files modified in the last 7 days"
ov-prompter --question "Summarize disk usage under /var/log"

# Or with the alias:
AI "Show me the 10 largest files in /var"
```

## Usage

### Basic commands

```bash
# Ask a question (required)
ov-prompter --question "List all files larger than 100MB"

# Override model for one query
ov-prompter --model-path ./path/to/models/TinyLlama --question "How to check free disk space"

# Force specific device
ov-prompter --device CPU --question "Find TODO comments in Python files"
```

### Verbosity levels

Control output detail with `-v` flags:

| Flag   | Output                                             |
|--------|----------------------------------------------------|
| (none) | Command only (default)                             |
| `-v`   | Command + brief summary                            |
| `-vv`  | Command + summary + detailed explanation           |
| `-vvv` | Command + summary + details + potential pitfalls   |

**Example:**

```bash
ov-prompter -vv --question "Archive logs older than 30 days"
# Output:
# tar czf old-logs.tar.gz $(find /var/log -mtime +30)
# Summary: Creates compressed archive of log files modified more than 30 days ago
# Detail: Uses find to locate files, pipes to tar for compression with gzip
```

### Configuration

Settings persist in `~/.config/ov-prompter`:

```bash
# Set default model (avoids --model-path every time)
ov-prompter --set-model-default ./path/to/models/Phi-4-mini

# Set device priority (tries GPU first, falls back to CPU)
ov-prompter --set-device-default GPU,CPU

# Customize system prompt for specialized tasks
ov-prompter --set-system-prompt-default "You are a security-focused sysadmin."

# View current system prompt
ov-prompter --show-system-prompt
```

### CLI reference

| Flag                                     | Description                                     |
|------------------------------------------|-------------------------------------------------|
| `--question <text>`                      | Natural language query (required for inference) |
| `--model-path <path>`                    | Override default model directory                |
| `--device <list>`                        | Device priority (e.g., `GPU,CPU`, `NPU,CPU`)    |
| `--set-model-default <path>`             | Persist model path to config                    |
| `--set-device-default <list>`            | Persist device priority to config               |
| `--set-system-prompt-default <text>`     | Persist custom system prompt                    |
| `--show-system-prompt`                   | Display active system prompt                    |
| `-v`, `-vv`, `-vvv`                      | Increase verbosity (summary, details, pitfalls) |
| `--verbosity <0-3>`                      | Set verbosity level explicitly                  |

## Building from Source

### Prerequisites

```bash
lsb_release -ds  # Ubuntu 22.04+ recommended
cmake --version  # ≥ 3.23 required
g++ --version    # C++17 support required
```

### Build steps

1. **Download prebuilt dependencies:**

   ```bash
   ./download-ext-libs.sh
   ```

   Fetches matching prebuilt versions of OpenVINO© runtime, GenAI, and Tokenizers from official sources.

2. **Compile:**

   ```bash
   ./build.sh
   ```

3. **Install (optional):**

   ```bash
   sudo ./build.sh install  # Installs to /usr/local by default
   # Or custom prefix:
   PREFIX=/opt/ov-prompter make install
   ```

### Build outputs

- `build/ov-prompter` – Standalone binary
- `libs/` – Bundled runtime libraries (portable, can be copied to other machines)

The binary uses RPATH to find libraries, so `./build/ov-prompter` runs directly without wrappers.

## Troubleshooting

**GPU initialization fails:**

- Ensure Intel GPU drivers are installed: `level-zero-loader`, `intel-compute-runtime`
- Force CPU if needed: `ov-prompter --device CPU --question "..."`
- Check driver status: `clinfo` or `sycl-ls`

**Missing libraries at runtime:**

- Verify `libs/` directory exists alongside the binary
- Check library contents: `ls libs/libopenvino*.so`

**Model not found:**

- Confirm model path: `ls -la ./path/to/models/Phi-4-mini`
- Set default: `ov-prompter --set-model-default ./path/to/models/Phi-4-mini`

**Build errors:**

- Ensure CMake ≥ 3.23: `cmake --version`
- Re-download dependencies: `rm -rf ext_libs && ./download-ext-libs.sh`

## Performance Tips

- **Use quantized models** (int8/int4) for 2-4× faster inference with minimal quality loss
- **GPU acceleration** works best for models >3B parameters; smaller models run fine on CPU
- **NPU support** available on Intel Core Ultra (Meteor Lake+) for power-efficient inference
- **Model selection**: Phi-4-mini offers best quality/speed balance; TinyLlama for maximum speed

## Packaging

- **Arch Linux (AUR)**: `packaging/aur/PKGBUILD`
- **Debian/Ubuntu**: `packaging/debian/` (use `dpkg-buildpackage` or `./build-deb.sh`)

## Contributing

Contributions welcome! Please open an issue or pull request for bugs, features, or improvements.

## Acknowledgements

This project bundles OpenVINO©, OpenVINO© GenAI, and OpenVINO© Tokenizers binaries that are distributed under the Apache License 2.0. These components are copyright © Intel Corporation and their respective contributors.

This project is not affiliated, endorsed or sponsorized by Intel Corporation.

## License

Apache 2.0. Third-party components inside `libs/` retain their original licenses (OpenVINO©, OpenVINO© GenAI, tokenizers, ICU, etc.).

## Author

- **Aleritty** – [aleritty.net](https://aleritty.net)

## Usage acknowledgement

If you build something with ov-prompter or deploy it in production, I’d love to hear from you. Feel free to get in touch.
