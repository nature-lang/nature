#!/usr/bin/env bash
set -e

# -----------------------------
# Nature ASCII Banner
# -----------------------------
echo " _   _       _                 "
echo "| \\ | | __ _| |_ _   _ _ __    "
echo "|  \\| |/ _\` | __| | | | '_ \\ "
echo "| |\\  | (_| | |_| |_| | | | |  "
echo "|_| \\_|\\__,_|\\__|\\__,_|_| |_| "
echo "          Nature Programming Language"
echo

echo "Starting Nature installer..."

# -----------------------------
# Detect OS
# -----------------------------
OS=$(uname -s | tr '[:upper:]' '[:lower:]')

case "$OS" in
  linux) OS="linux" ;;
  darwin) OS="darwin" ;;
  *)
    echo "❌ Unsupported OS: $OS"
    exit 1
    ;;
esac

# -----------------------------
# Detect Architecture
# -----------------------------
ARCH=$(uname -m)

case "$ARCH" in
  x86_64) ARCH="amd64" ;;
  aarch64|arm64) ARCH="arm64" ;;
  riscv64) ARCH="riscv64" ;;
  *)
    echo "❌ Unsupported architecture: $ARCH"
    exit 1
    ;;
esac

echo "Detected: $OS-$ARCH"

# -----------------------------
# Build asset pattern
# -----------------------------
ASSET_PATTERN="${OS}-${ARCH}.*tar.gz"
echo "Looking for asset: $ASSET_PATTERN"

# -----------------------------
# Fetch latest release asset URL
# -----------------------------
LATEST_URL=$(curl -s https://api.github.com/repos/nature-lang/nature/releases/latest \
  | grep "browser_download_url" \
  | grep -i "$ASSET_PATTERN" \
  | cut -d '"' -f 4)

if [[ -z "$LATEST_URL" ]]; then
  echo "❌ Could not find a matching release asset."
  exit 1
fi

echo "Found release: $LATEST_URL"

# -----------------------------
# Download
# -----------------------------
FILE=$(basename "$LATEST_URL")
echo "Downloading $FILE..."
curl -L "$LATEST_URL" -o "$FILE"

# -----------------------------
# Install to /usr/local/nature
# -----------------------------
INSTALL_DIR="/usr/local/nature"

echo "Extracting to $INSTALL_DIR..."
sudo mkdir -p "$INSTALL_DIR"
sudo tar -xzf "$FILE" -C /usr/local

rm "$FILE"

# -----------------------------
# Update PATH
# -----------------------------
PROFILE="$HOME/.bashrc"
[[ "$OS" == "darwin" ]] && PROFILE="$HOME/.zshrc"

if ! grep -q "/usr/local/nature/bin" "$PROFILE"; then
  echo "Updating PATH in $PROFILE..."
  echo 'export PATH="/usr/local/nature/bin:$PATH"' >> "$PROFILE"
fi

echo "✅ Nature installed successfully!"
echo "Restart your terminal to begin using it."
