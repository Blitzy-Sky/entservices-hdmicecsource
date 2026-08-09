"""
/**
 * @file TCID09_Set_Enabled_True.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID09_Set_Enabled_True
 * @details Sends org.rdk.HdmiCecSource.setEnabled with params enabled true and compares the
 *          whole envelope against a success reply. The reply is compared as a WHOLE ENVELOPE,
 *          so the jsonrpc member and the request id 42 are part of the contract and not only
 *          the result body.
 *
 *          Idempotent by construction, which is what makes the position safe wherever it
 *          lands: the plugin's SetEnabled accepts the request whether or not CEC is already
 *          enabled, so the case behaves identically after position 08's restore and on a
 *          target that was already enabled. Nothing has to be undone, because enabled is the
 *          state the rest of the suite expects.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - None beyond the plugin being reachable; the case is valid from either CEC state.
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

    log_info("Executing the curl command set enabled params TRUE")

    curl_response = send_curl_command(
        HdmiCecSourceApis.set_enabled_true
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        if json.loads(curl_response) == expected_output_response:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID09_Set_Enabled_True Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
        else:
            log_error(f"TCID09_Set_Enabled_True Failed ❌")
            return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID09_Set_Enabled_True Failed ❌")
        return False
