"""
/**
 * @file TCID14_Verify_Vendor_ID_Readback.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID14_Verify_Vendor_ID_Readback
 * @details Calls org.rdk.HdmiCecSource.getVendorId and compares the whole envelope against
 *          {"jsonrpc":"2.0","id":42,"result":{"vendorid":"019fb","success":true}} - the
 *          rendering of the 0x0019FB that position 13 writes. The reply is compared as a WHOLE
 *          ENVELOPE, so the jsonrpc member and the request id 42 are part of the contract and
 *          not only the result body.
 *
 *          What the readback pins is that the value survives the write path and is rendered by
 *          CECBytes::toString() without a 0x prefix and without the first byte's leading zero.
 *          Because 0x0019FB is ALSO the plugin's provisioned default, this case cannot
 *          distinguish a successful write from an untouched default on its own; the pairing
 *          with position 13 is what gives it meaning, and it therefore needs the suite order
 *          rather than a name filter.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - Position 13 has run in the same suite execution, or the target's persisted cecVendorId
 *    already holds 0x0019FB.
 *  - Authored for device-level execution and NOT executed: every criterion below states what
 *    this module asserts, not an observed result. README.txt.txt records the deferred status
 *    and the prerequisites that are unavailable.
 *
 * @dependencies
 *  - utils.py - send_curl_command and the logging helpers.
 *  - HdmiCECSource_Curl.py - the JSON-RPC request constants this module dispatches.
 *  - SuitManager.py - the runner that registers this module and calls run_test().
 *
 * @expected_result
 *  - The reply parses as JSON and equals the envelope above exactly.
 *
 * @pass_criteria
 *  - The response is non-empty, the parsed reply equals that envelope exactly, and run_test()
 *    returns True.
 *
 * @failure_criteria
 *  - Empty response, a vendorid other than the rendering of 0x0019FB, any other envelope
 *    difference, a JSONDecodeError, or run_test() returns False.
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
            msg = f"TCID14_Verify_Vendor_ID_Readback Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
        else:
            log_error(f"TCID14_Verify_Vendor_ID_Readback Failed ❌")
            return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID14_Verify_Vendor_ID_Readback Failed ❌")
        return False
