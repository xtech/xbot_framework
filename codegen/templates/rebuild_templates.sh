#!/bin/bash
set -euo pipefail
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
echo $SCRIPT_DIR
cd $SCRIPT_DIR
source ../../../../build/Debug/ext/xbot_framework/codegen/.venv/bin/activate
python3 -m cogapp -I ../xbot_codegen -D service_file=service.json -r @cogfiles.txt
