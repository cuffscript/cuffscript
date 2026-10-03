#!/usr/bin/env bash
# Builds cuffsh (the interactive shell) against the CuffScript engine
# exactly as shipped in ../engine — nothing under the project root is
# modified by this script. Run from anywhere.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

CXX="${CXX:-g++}"
"$CXX" -std=c++17 -Wall -Wextra -O2 -o cuffsh main.cpp

echo "Built cli/cuffsh"
