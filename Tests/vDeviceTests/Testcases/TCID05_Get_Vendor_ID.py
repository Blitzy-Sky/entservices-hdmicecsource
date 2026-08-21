"""
/**
 * @file TCID05_Get_Vendor_ID.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID05_Get_Vendor_ID
 * @details Calls org.rdk.HdmiCecSource.getVendorId and compares the whole envelope against
 *          {"jsonrpc":"2.0","id":42,"result":{"vendorid":"019fb","success":true}}. The reply
 *          is compared as a WHOLE ENVELOPE, so the jsonrpc member and the request id 42 are
 *          part of the contract and not only the result body.
 *
 *          The expected "019fb" is derived, not guessed. GetVendorId returns
 *          appVendorId.toString(); appVendorId is initialised from the plugin's
 *          defaultVendorId bytes {0x00,0x19,0xFB} and is overwritten from the persisted
 *          cecVendorId setting when one is present; and CECBytes::toString() renders those
 *          three bytes without a 0x prefix and without the leading zero of the first byte.
 *          This position therefore reads the PROVISIONED vendor id - position 13 writes the
 *          same constant and position 14 reads it back.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - The target's persisted cecVendorId is the provisioned default 0x0019FB. A target whose
 *    settings file holds a different vendor id will report that value and the case will say
 *    so.
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
 *  - Empty response, a vendorid other than the rendering of the provisioned value, any other
 *    envelope difference, a JSONDecodeError, or run_test() returns False.
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
