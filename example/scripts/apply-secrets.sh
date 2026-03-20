#!/bin/bash
# Apply WiFi secrets to sdkconfig without committing them

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SECRETS_FILE="$PROJECT_DIR/.secrets/wifi.conf"

if [ ! -f "$SECRETS_FILE" ]; then
    echo "❌ Secrets file not found: $SECRETS_FILE"
    echo "Create it with:"
    echo "  mkdir -p $PROJECT_DIR/.secrets"
    echo "  cat > $SECRETS_FILE << 'EOF'"
    echo "WIFI_SSID=\"your_ssid\""
    echo "WIFI_PASSWORD=\"your_password\""
    echo "FIBER_RPC_URL=\"http://192.168.68.105:8227\""
    echo "TARGET_URL=\"http://httpbin.org/status/402\""
    echo "EOF"
    exit 1
fi

# Load secrets
source "$SECRETS_FILE"

echo "🔐 Applying WiFi secrets to build config..."

# Remove old sdkconfig (will be regenerated from defaults + secrets)
rm -f "$PROJECT_DIR/sdkconfig"

# Create temporary sdkconfig with secrets
cat > "$PROJECT_DIR/sdkconfig.local" << EOF
# Auto-generated from .secrets/wifi.conf - DO NOT COMMIT
CONFIG_EXAMPLE_WIFI_SSID="$WIFI_SSID"
CONFIG_EXAMPLE_WIFI_PASS="$WIFI_PASSWORD"
CONFIG_EXAMPLE_FIBER_RPC_URL="$FIBER_RPC_URL"
CONFIG_EXAMPLE_TARGET_URL="$TARGET_URL"
EOF

echo "✓ Secrets applied to sdkconfig.local"
echo ""
echo "Next steps:"
echo "  cd $PROJECT_DIR"
echo "  idf.py build flash monitor"
echo ""
echo "⚠️  Make sure .secrets/ and sdkconfig.local are in .gitignore"
