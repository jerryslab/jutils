#!/bin/bash
#
# Author: Jerry Richardson (jerry@jerryslab.com)
# (C) Copyright 2025
#
#

# --- Argument Check ---
if [[ -z "$1" ]]; then
    echo "Usage: $0 <kernel-version>"
    echo "Example: $0 6.16.5-jerryslab.patch-GIT12-v3.16+"
    exit 1
fi

KERNEL="$1"

# --- Confirm that files exist ---
FILES=(
    "/boot/config-${KERNEL}"
    "/boot/initrd.img-${KERNEL}"
    "/boot/System.map-${KERNEL}"
    "/boot/vmlinuz-${KERNEL}"
    "/lib/modules/${KERNEL}"
)

echo "Checking kernel components for version: ${KERNEL}"
for f in "${FILES[@]}"; do
    if [[ ! -e "$f" ]]; then
        echo "ERROR: Missing file or directory: $f"
        exit 2
    fi
done

# --- Create archive ---
ARCHIVE="${KERNEL}.tar.xz"
echo "Creating archive: $ARCHIVE"

sudo tar cfJ "$ARCHIVE" "${FILES[@]}"
RET=$?

if [[ $RET -ne 0 ]]; then
    echo "ERROR: tar failed! Kernel files will NOT be removed."
    exit 3
fi

echo "Archive created successfully."

# --- Prompt for removal ---
read -rp "Remove kernel ${KERNEL} from /boot and /lib/modules? (y/N): " ANSWER

case "$ANSWER" in
    y|Y|yes|YES)
        echo "Removing kernel files..."
        sudo rm -rvf "${FILES[@]}"
        echo "Kernel ${KERNEL} removed."
        ;;
    *)
        echo "Aborted. Kernel files NOT removed."
        ;;
esac

