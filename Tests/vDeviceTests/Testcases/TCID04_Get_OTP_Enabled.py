"""
/**
 * @file TCID04_Get_OTP_Enabled.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID04_Get_OTP_Enabled
 * @details Writes its own precondition before reading. setOTPEnabled(enabled=true) is sent
 *          first and its reply is deliberately NOT inspected, so this position cannot fail for
 *          a reason that belongs to position 12; then getOTPEnabled is called and the NESTED
 *          fields are asserted - result.success exactly True and result.enabled exactly True.
 *
 *          Reading the nested members rather than comparing a whole envelope is what allows
 *          the precondition write and the read to be separate steps; the id and jsonrpc
 *          members are not part of this case's contract.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - None beyond the plugin being reachable. The one-time-play setting the read expects is
 *    established by this module itself.
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
 *  - The read reply parses as JSON and carries result.success true with result.enabled true.
 *
 * @pass_criteria
 *  - The read response is non-empty, result.success is True, result.enabled is True, and
 *    run_test() returns True.
 *
 * @failure_criteria
 *  - The read returns nothing, success is not exactly True, enabled is not exactly True, or a
 *    JSONDecodeError is raised.
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

    # Deterministic precondition for validation.
    send_curl_command(HdmiCecSourceApis.set_otp_enabled_true)

    log_info("Executing the curl command get OTP enabled")

    curl_response = send_curl_command(
        HdmiCecSourceApis.get_otp_enabled
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        body = json.loads(curl_response)
        result = body.get("result", {})
        if result.get("success") is True and result.get("enabled") is True:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID04_Get_OTP_Enabled Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True

        log_error(f"TCID04_Get_OTP_Enabled Failed ❌")
        return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID04_Get_OTP_Enabled Failed ❌")
        return False
