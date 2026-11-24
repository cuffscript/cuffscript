#!/usr/bin/env bash
# Builds cuffsh (the interactive shell) against the CuffScript engine
# exactly as shipped in ../engine — nothing under the project root is
# modified by this script. Run from anywhere.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

CXX="${CXX:-g++}"

# EXCLUDE_DLC="network hardware" leaves those libraries out, as `make EXCLUDE_DLC=...` does for cuffc.
DLC_NAMES=$(sed -n 's/^DLC_NAMES = //p' ../engine/dlc/libraries.txt | tr -d '\r') # the libraries that can be left out
if [ -z "$DLC_NAMES" ]; then
    echo "could not read the DLC_NAMES line of engine/dlc/libraries.txt" >&2
    exit 1
fi
DLC_FLAGS=()
for dlc in ${EXCLUDE_DLC:-}; do
    case " $DLC_NAMES " in
    *" $dlc "*) DLC_FLAGS+=("-DCUFF_EXCLUDE_DLC_$(printf '%s' "$dlc" | tr '[:lower:]' '[:upper:]')") ;;
    *)
        echo "EXCLUDE_DLC cannot leave out $dlc (those that can be left out are: $DLC_NAMES)" >&2
        exit 1
        ;;
    esac
done

"$CXX" -std=c++17 -Wall -Wextra -O2 ${DLC_FLAGS[@]+"${DLC_FLAGS[@]}"} -o cuffsh main.cpp

echo "Built cli/cuffsh"
