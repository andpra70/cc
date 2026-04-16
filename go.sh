#!/bin/bash
set -euo pipefail

TMP_DIR="./tmp"
mkdir -p "$TMP_DIR"

echo "[1/3] Build iniziale con make"
make -B cc

echo "[2/3] Il compilatore buildato da make compila cc.c via stdin/stdout"
./cc < cc.c > "$TMP_DIR/cc-self1"
chmod +x "$TMP_DIR/cc-self1"

echo "[3/3] Il compilatore self-hosted ricompila cc.c e deve funzionare"
"$TMP_DIR/cc-self1" < cc.c > "$TMP_DIR/cc-self2"
chmod +x "$TMP_DIR/cc-self2"
"$TMP_DIR/cc-self2" < cc.c > "$TMP_DIR/cc-self3"
chmod +x "$TMP_DIR/cc-self3"

cat > "$TMP_DIR/smoke.c" << 'EOF'
int main() { return 7; }
EOF

"$TMP_DIR/cc-self3" < "$TMP_DIR/smoke.c" > "$TMP_DIR/smoke.out"
chmod +x "$TMP_DIR/smoke.out"
set +e
"$TMP_DIR/smoke.out"
rc=$?
set -e
if [ "$rc" -ne 7 ]; then
  echo "ERRORE: smoke test fallito (exit=$rc, atteso=7)"
  exit 1
fi

cp "$TMP_DIR/cc-self3" ./cc
echo "OK: make -> self1 -> self2 -> self3 via stdin/stdout, smoke test passato"
