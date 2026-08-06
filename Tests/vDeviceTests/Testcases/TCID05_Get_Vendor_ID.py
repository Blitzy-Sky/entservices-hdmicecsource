"""
/**
 * @file TCID05_Get_Vendor_ID.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Reads the configured vendor ID.
 *
 * @testcase TCID05_Get_Vendor_ID
 * @details Invokes getVendorId and checks a vendorid value is returned with success.
 *
 * @precondition
 *  - A device under test - physical hardware or a QEMU target - is running WPEFramework
 *    with the org.rdk.HdmiCecSource plugin activated and reachable over JSON-RPC.
 *
 * @dependencies
 *  - utils.py - shared endpoint resolution, curl dispatch and logging helpers
 *  - HdmiCECSource_Curl.py - the JSON-RPC command strings this module dispatches
 *  - SuitManager.py - registers and runs this module
 *
 * @expected_result
 *  - org.rdk.HdmiCecSource.getVendorId answers with the values this scenario expects.
 *
 * @pass_criteria
 *  - Every response matches its expected value and run_test() returns True.
 *
 * @failure_criteria
 *  - A response mismatch, a JSON-RPC or JSON parsing failure, an unreachable endpoint
 *    or an unavailable device-level prerequisite; run_test() then returns False.
 */
"""


import time
import os


import json
from utils import (
    send_curl_command,
        log_info,
        log_success,
        log_error,
        log_warning,
    log_with_timing
)
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    expected_output_response = {
    "jsonrpc": "2.0",
    "id": 42,
    "result": {
        "vendorid": "019fb",
        "success": True
        }
    }

    log_info("Executing the curl command get vendor ID")

    curl_response = send_curl_command(
        HdmiCecSourceApis.get_vendor_id
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        if json.loads(curl_response) == expected_output_response:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID05_Get_Vendor_ID Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
        else:
            log_error(f"TCID05_Get_Vendor_ID Failed ❌")
            return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID05_Get_Vendor_ID Failed ❌")
        return False
