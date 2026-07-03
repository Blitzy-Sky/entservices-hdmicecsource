#!/bin/sh
#
# /**
#  * @file Profile.sh
#  * @brief Profile.sh
#  *
#  * @testcase Profile
#  * @details Sets the RDK profile in /etc/device.properties for the HDMI CEC Source
#  *          test flow. Updates an existing RDK_PROFILE entry or creates it when absent.
#  *
#  * @precondition
#  *  - Script is executed with a supported profile argument.
#  *  - Write permission to /etc/device.properties is available.
#  *
#  * @dependencies
#  *  - /etc/device.properties
#  *  - sed
#  *  - grep
#  *
#  * @expected_result
#  *  - RDKprofile is set to STB.
#  *
#  * @pass_criteria
#  *  - Script exits with code 0 and prints the updated profile value.
#  *
#  * @failure_criteria
#  *  - Invalid profile argument is supplied or file update fails.
#  */

set -e

DEVICE_PROPS="/etc/device.properties"
PROFILE="$1"

# Validate argument
case "$PROFILE" in
    STB)
        ;;
    *)
        echo "Usage: $0 STB"
        exit 1
        ;;
esac

if [ -f "$DEVICE_PROPS" ]; then
    if grep -q '^RDK_PROFILE=' "$DEVICE_PROPS"; then
        sed -i "s/^RDK_PROFILE=.*/RDK_PROFILE=${PROFILE}/" "$DEVICE_PROPS"
    else
        printf '\nRDK_PROFILE=%s\n' "$PROFILE" >> "$DEVICE_PROPS"
    fi
else
    printf 'RDK_PROFILE=%s\n' "$PROFILE" > "$DEVICE_PROPS"
fi

echo "RDK_PROFILE set to ${PROFILE}"

