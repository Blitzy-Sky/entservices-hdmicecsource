"""
/**
 * @file TCID13_Set_Vendor_ID.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID13_Set_Vendor_ID
 * @details Sends org.rdk.HdmiCecSource.setVendorId with params vendorid "0x0019FB" and
 *          compares the whole envelope against a success reply. The reply is compared as a
 *          WHOLE ENVELOPE, so the jsonrpc member and the request id 42 are part of the
 *          contract and not only the result body.
 *
 *          The plugin parses the string with stoi(value, NULL, 16), packs the low three bytes
 *          into appVendorId and persists the number under cecVendorId, which is the path that
 *          lets position 14 read the value back. The constant is the plugin's own provisioned
 *          default, so what this position establishes is that the write is ACCEPTED - not that
 *          it changed the value.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - None beyond the plugin being reachable.
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
 *  - The reply parses as JSON and equals {"jsonrpc":"2.0","id":42,"result":{"success":true}}
 *    exactly.
 *
 * @pass_criteria
 *  - The response is non-empty, the parsed reply equals that envelope exactly, and run_test()
 *    returns True.
 *
 * @failure_criteria
 *  - Empty response, any envelope difference, a JSONDecodeError, or run_test() returns False.
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
            "success": True
        }
    }

    log_info("Executing the curl command set vendor ID")

    curl_response = send_curl_command(
        HdmiCecSourceApis.set_vendor_id
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        if json.loads(curl_response) == expected_output_response:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID13_Set_Vendor_ID Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
        else:
            log_error(f"TCID13_Set_Vendor_ID Failed ❌")
            return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID13_Set_Vendor_ID Failed ❌")
        return False
