#!/bin/bash
#
# Unified script to set DAX device alignment for both pmem and non-pmem DAX devices
# Automatically detects device type and applies the appropriate method
#
# Usage: ./dax-set-alignment.sh <device> <alignment> [node] [region]
#
# Examples:
#   ./dax-set-alignment.sh /dev/dax2.0 2m
#   ./dax-set-alignment.sh dax2.0 4k
#   ./dax-set-alignment.sh /dev/dax1.0 2m famfs3
#   ./dax-set-alignment.sh dax1.0 2m famfs2 region1
#

set -e  # Exit on error

# Check for required arguments
if [ $# -lt 2 ]; then
    echo "Error: Missing required arguments"
    echo ""
    echo "Usage: $0 <device> <alignment> [node] [region]"
    echo ""
    echo "Arguments:"
    echo "  device     - DAX device path or name (e.g., /dev/dax2.0 or dax2.0)"
    echo "  alignment  - Alignment size (e.g., 2m, 4k, 1g)"
    echo "  node       - Remote node name (default: current node)"
    echo "  region     - NVDIMM region for pmem devices (default: auto-detect)"
    echo ""
    echo "Examples:"
    echo "  $0 /dev/dax2.0 2m"
    echo "  $0 dax2.0 4k"
    echo "  $0 /dev/dax1.0 2m famfs3"
    echo "  $0 dax1.0 2m famfs2 region1"
    echo ""
    echo "This script automatically detects whether the device is pmem-backed"
    echo "or native DAX and applies the appropriate reconfiguration method."
    exit 1
fi

# Required arguments
DEVICE_INPUT="$1"
ALIGNMENT="$2"

# Optional arguments with defaults
NODE="${3:-}"  # Default to current node if not specified
REGION="${4:-}"  # Will auto-detect if not specified and device is pmem

# Extract device name (strip /dev/ if present)
DEVICE=$(basename "$DEVICE_INPUT")

# Set up command prefix for local vs remote execution
if [ -z "${NODE}" ]; then
    CMD_PREFIX=""
    NODE_DISPLAY="current node"
else
    CMD_PREFIX="ssh ${NODE}"
    NODE_DISPLAY="node ${NODE}"
fi

# Convert alignment to bytes
case "${ALIGNMENT}" in
    4k|4K)
        BYTES="4096"
        DISPLAY="4KiB"
        ;;
    2m|2M)
        BYTES="2097152"
        DISPLAY="2MiB"
        ;;
    1g|1G)
        BYTES="1073741824"
        DISPLAY="1GiB"
        ;;
    *)
        # Assume it's already in bytes
        BYTES="${ALIGNMENT}"
        DISPLAY="${ALIGNMENT} bytes"
        ;;
esac

echo "Detecting device type for ${DEVICE} on ${NODE_DISPLAY}..."
echo ""

# Function to detect if device is pmem-backed
detect_device_type() {
    local dev="$1"

    # Check if device appears in ndctl namespaces (pmem-backed)
    # Look specifically for the chardev matching our device
    if [ -z "${NODE}" ]; then
        # Local execution
        HAS_NAMESPACE=$(ndctl list -N 2>/dev/null | grep -c "\"chardev\":\"${dev}\"" || true)
    else
        # Remote execution
        HAS_NAMESPACE=$(ssh ${NODE} "ndctl list -N 2>/dev/null | grep -c '\"chardev\":\"${dev}\"'")
    fi

    if [ "$HAS_NAMESPACE" != "0" ]; then
        echo "pmem"
    else
        echo "native"
    fi
}

DEVICE_TYPE=$(detect_device_type "$DEVICE")

echo "Device type: ${DEVICE_TYPE}"
echo "Device: ${DEVICE}"
echo "Alignment: ${DISPLAY} (${BYTES} bytes)"
echo ""

if [ "$DEVICE_TYPE" == "pmem" ]; then
    # PMEM-backed DAX device - use ndctl
    echo "Using ndctl method for pmem device..."
    echo ""

    # Find the namespace
    if [ -z "${NODE}" ]; then
        NAMESPACE=$(ndctl list -D | grep -B10 "\"chardev\":\"${DEVICE}\"" | grep "\"dev\":" | head -1 | cut -d'"' -f4)
    else
        NAMESPACE=$(ssh ${NODE} "ndctl list -D | grep -B10 '\"chardev\":\"${DEVICE}\"' | grep '\"dev\":' | head -1 | cut -d'\"' -f4")
    fi

    if [ -z "$NAMESPACE" ]; then
        echo "Namespace not currently configured, will auto-detect..."
        # Extract namespace number from device (e.g., dax1.0 -> namespace1.0)
        NAMESPACE="namespace${DEVICE#dax}"
        echo "Using namespace: ${NAMESPACE}"
    fi

    # Auto-detect region if not specified
    if [ -z "$REGION" ]; then
        echo "Auto-detecting region..."
        if [ -z "${NODE}" ]; then
            REGION=$(ndctl list -R | grep "\"dev\":" | head -1 | cut -d'"' -f4)
        else
            REGION=$(ssh ${NODE} "ndctl list -R | grep '\"dev\":' | head -1 | cut -d'\"' -f4")
        fi

        if [ -z "$REGION" ]; then
            echo "Error: Could not auto-detect region"
            exit 1
        fi
        echo "Detected region: ${REGION}"
    fi

    echo "Namespace: ${NAMESPACE}"
    echo "Region: ${REGION}"
    echo ""

    # Execute the reconfiguration
    echo "Reconfiguring via ndctl..."
    if [ -z "${NODE}" ]; then
        sudo ndctl destroy-namespace ${NAMESPACE} --force && \
        sudo ndctl create-namespace --mode=devdax --align=${ALIGNMENT} --region=${REGION}
    else
        ssh ${NODE} "sudo ndctl destroy-namespace ${NAMESPACE} --force && \
        sudo ndctl create-namespace --mode=devdax --align=${ALIGNMENT} --region=${REGION}"
    fi

    echo ""
    echo "Verifying new configuration..."
    ${CMD_PREFIX} ndctl list -N | grep -A 7 "\"${NAMESPACE}\"" || true

else
    # Native DAX device - use sysfs method
    echo "Using sysfs method for native DAX device..."
    echo ""

    # Check if device exists
    echo "Verifying device exists..."
    ${CMD_PREFIX} test -e /sys/bus/dax/devices/${DEVICE} || {
        echo "Error: Device ${DEVICE} not found on ${NODE_DISPLAY}"
        exit 1
    }

    # Get current configuration
    echo "Current configuration:"
    ${CMD_PREFIX} daxctl list | grep -A 5 "\"${DEVICE}\"" || true
    echo ""

    # Check if device is currently enabled (bound to driver)
    echo "Checking device state..."
    if [ -z "${NODE}" ]; then
        DEVICE_STATE=$(daxctl list | grep -A5 "\"chardev\":\"${DEVICE}\"" | grep "\"state\":" | cut -d'"' -f4)
        CURRENT_ALIGN=$(cat /sys/bus/dax/devices/${DEVICE}/align)
    else
        DEVICE_STATE=$(ssh ${NODE} "daxctl list | grep -A5 '\"chardev\":\"${DEVICE}\"' | grep '\"state\":' | cut -d'\"' -f4")
        CURRENT_ALIGN=$(ssh ${NODE} "cat /sys/bus/dax/devices/${DEVICE}/align")
    fi

    # If no state field, device is enabled; if state is "disabled", it's disabled
    if [ -z "$DEVICE_STATE" ]; then
        DEVICE_STATE="enabled"
    fi

    echo "Current state: ${DEVICE_STATE}"
    echo "Current alignment: ${CURRENT_ALIGN} bytes"

    # Check if alignment already matches
    ALIGNMENT_MATCHES=0
    if [ "$CURRENT_ALIGN" == "$BYTES" ]; then
        ALIGNMENT_MATCHES=1
        echo "Alignment already correct (${DISPLAY})"
    fi

    # Unbind if needed, change alignment, and bind
    echo "Reconfiguring device..."
    if [ -z "${NODE}" ]; then
        # Unbind only if currently enabled
        if [ "$DEVICE_STATE" == "enabled" ]; then
            echo "Unbinding device..."
            echo ${DEVICE} | sudo tee /sys/bus/dax/drivers/device_dax/unbind > /dev/null
        else
            echo "Device already unbound"
        fi

        # Only set alignment if it needs to change
        if [ "$ALIGNMENT_MATCHES" == "0" ]; then
            # Check if align file is writable (it usually isn't for native dax after unbind)
            if [ -w /sys/bus/dax/devices/${DEVICE}/align ]; then
                echo "Setting alignment via sysfs..."
                echo ${BYTES} | sudo tee /sys/bus/dax/devices/${DEVICE}/align > /dev/null
                echo "Binding device..."
                echo ${DEVICE} | sudo tee /sys/bus/dax/drivers/device_dax/bind > /dev/null
            else
                # Use daxctl to destroy and recreate with new alignment
                echo "Align file is read-only, using daxctl to reconfigure..."

                # Get device size and region
                DEV_SIZE=$(cat /sys/bus/dax/devices/${DEVICE}/size)

                # Disable and destroy the device
                echo "Disabling and destroying device..."
                sudo daxctl disable-device ${DEVICE}
                sudo daxctl destroy-device ${DEVICE}

                # Get the region ID from daxctl list
                REGION_ID=$(daxctl list -R | grep -B5 "\"size\":${DEV_SIZE}" | grep "\"id\":" | head -1 | cut -d: -f2 | tr -d ' ,')

                if [ -z "$REGION_ID" ]; then
                    echo "Error: Could not determine region ID"
                    exit 1
                fi

                echo "Recreating device in region ${REGION_ID} with ${DISPLAY} alignment..."
                echo "WARNING: Device name may change (e.g., dax${REGION_ID}.0 -> dax${REGION_ID}.1)"
                CREATE_OUTPUT=$(sudo daxctl create-device -r ${REGION_ID} -s ${DEV_SIZE} --align ${ALIGNMENT})
                echo "$CREATE_OUTPUT"
                NEW_DEVICE=$(echo "$CREATE_OUTPUT" | grep "chardev" | cut -d'"' -f4)
                if [ -n "$NEW_DEVICE" ] && [ "$NEW_DEVICE" != "$DEVICE" ]; then
                    echo ""
                    echo "NOTICE: Device was recreated with new name: ${NEW_DEVICE}"
                    echo "        Old device: /dev/${DEVICE}"
                    echo "        New device: /dev/${NEW_DEVICE}"
                fi
            fi
        else
            echo "Binding device..."
            echo ${DEVICE} | sudo tee /sys/bus/dax/drivers/device_dax/bind > /dev/null
        fi
    else
        # Remote execution
        if [ "$DEVICE_STATE" == "enabled" ]; then
            echo "Unbinding device..."
            ssh ${NODE} "echo ${DEVICE} | sudo tee /sys/bus/dax/drivers/device_dax/unbind > /dev/null"
        else
            echo "Device already unbound"
        fi

        # Only set alignment if it needs to change
        if [ "$ALIGNMENT_MATCHES" == "0" ]; then
            # Check if align file is writable
            ALIGN_WRITABLE=$(ssh ${NODE} "test -w /sys/bus/dax/devices/${DEVICE}/align && echo 1 || echo 0")
            if [ "$ALIGN_WRITABLE" == "1" ]; then
                echo "Setting alignment via sysfs..."
                ssh ${NODE} "echo ${BYTES} | sudo tee /sys/bus/dax/devices/${DEVICE}/align > /dev/null"
                echo "Binding device..."
                ssh ${NODE} "echo ${DEVICE} | sudo tee /sys/bus/dax/drivers/device_dax/bind > /dev/null"
            else
                # Use daxctl to destroy and recreate with new alignment
                echo "Align file is read-only, using daxctl to reconfigure..."

                # Get device size
                DEV_SIZE=$(ssh ${NODE} "cat /sys/bus/dax/devices/${DEVICE}/size")

                # Disable and destroy the device
                echo "Disabling and destroying device..."
                ssh ${NODE} "sudo daxctl disable-device ${DEVICE}"
                ssh ${NODE} "sudo daxctl destroy-device ${DEVICE}"

                # Get the region ID from daxctl list
                REGION_ID=$(ssh ${NODE} "daxctl list -R | grep -B5 '\"size\":${DEV_SIZE}' | grep '\"id\":' | head -1 | cut -d: -f2 | tr -d ' ,'")

                if [ -z "$REGION_ID" ]; then
                    echo "Error: Could not determine region ID"
                    exit 1
                fi

                echo "Recreating device in region ${REGION_ID} with ${DISPLAY} alignment..."
                echo "WARNING: Device name may change (e.g., dax${REGION_ID}.0 -> dax${REGION_ID}.1)"
                CREATE_OUTPUT=$(ssh ${NODE} "sudo daxctl create-device -r ${REGION_ID} -s ${DEV_SIZE} --align ${ALIGNMENT}")
                echo "$CREATE_OUTPUT"
                NEW_DEVICE=$(echo "$CREATE_OUTPUT" | grep "chardev" | cut -d'"' -f4)
                if [ -n "$NEW_DEVICE" ] && [ "$NEW_DEVICE" != "$DEVICE" ]; then
                    echo ""
                    echo "NOTICE: Device was recreated with new name: ${NEW_DEVICE}"
                    echo "        Old device: /dev/${DEVICE}"
                    echo "        New device: /dev/${NEW_DEVICE}"
                fi
            fi
        else
            echo "Binding device..."
            ssh ${NODE} "echo ${DEVICE} | sudo tee /sys/bus/dax/drivers/device_dax/bind > /dev/null"
        fi
    fi

    echo ""
    echo "Verifying new configuration..."
    ${CMD_PREFIX} daxctl list | grep -A 5 "\"${DEVICE}\"" || true
fi

echo ""
echo "Done! Device ${DEVICE} alignment set to ${DISPLAY} (${BYTES} bytes)"
