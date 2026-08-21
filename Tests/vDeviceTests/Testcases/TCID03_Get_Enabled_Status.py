"""
/**
 * @file TCID03_Get_Enabled_Status.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID03_Get_Enabled_Status
 * @details Calls org.rdk.HdmiCecSource.getEnabled and compares the whole envelope against
 *          {"jsonrpc":"2.0","id":42,"result":{"enabled":true,"success":true}}. The reply is
 *          compared as a WHOLE ENVELOPE, so the jsonrpc member and the request id 42 are part
 *          of the contract and not only the result body.
 *
 *          The enabled value is pinned to true rather than merely type-checked because it is
 *          the provisioned state: the plugin writes cecEnabled true into its settings file
 *          when the label is absent, and this position runs ahead of every case that disables
 *          CEC (08, 22, 23 and 29, each of which restores it). Run under a name filter after a
 *          disabling case the pinned value is still the correct expectation for a target in
 *          its default state, and the case reports the deviation instead of accommodating it.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - The target is in its provisioned CEC state, or a preceding position has left CEC enabled.
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
 *  - The reply parses as JSON and equals the envelope above exactly, reporting both enabled
 *    true and success true.
 *
 * @pass_criteria
 *  - The response is non-empty, the parsed reply equals that envelope exactly, and run_test()
 *    returns True.
 *
 * @failure_criteria
 *  - Empty response, enabled reported false, any other envelope difference, a JSONDecodeError,
 *    or run_test() returns False.
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
        "enabled": True,
        "success": True
        }
    }

    log_info("Executing the curl command get enabled Driver status")

    curl_response = send_curl_command(
        HdmiCecSourceApis.get_enabled
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        if json.loads(curl_response) == expected_output_response:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID03_Get_Enabled_Status Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
        else:
            elapsed_time = time.perf_counter() - start_time
            log_error(f"TCID03_Get_Enabled_Status Failed ❌")
            return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        elapsed_time = time.perf_counter() - start_time
        log_error(f"TCID03_Get_Enabled_Status Failed ❌")
        return False
