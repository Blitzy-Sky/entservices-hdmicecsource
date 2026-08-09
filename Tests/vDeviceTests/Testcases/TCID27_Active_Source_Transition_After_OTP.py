"""
/**
 * @file TCID27_Active_Source_Transition_After_OTP.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID27_Active_Source_Transition_After_OTP
 * @details Reads org.rdk.HdmiCecSource.getActiveSourceStatus for a baseline, posts
 *          Device_Add.yaml and Device_Status.yaml - BOTH REQUIRED - sends performOTPAction,
 *          then reads getActiveSourceStatus again.
 *
 *          THE VERDICT IS ON THE FINAL READ ONLY: result.success exactly True and
 *          result.status exactly True, meaning the device reports itself as the active source
 *          after the one-time-play walk. The baseline read must return a non-empty response,
 *          and its own status value is parsed and then deliberately discarded - it exists to
 *          show the reader answered before the transition, not to be compared with the final
 *          value.
 *
 *          The performOTPAction reply is required to be non-empty but is not parsed; a walk
 *          that reported failure while the status still flipped would pass, which is the
 *          honest reading of what the module checks.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - The vComponent API is reachable and serving the emulated CEC peers; HDMICEC_CMD_BASE
 *    resolves to the commands directory this module posts from.
 *  - CEC and the one-time-play setting are enabled, otherwise the walk is refused and the
 *    final status cannot become true.
 *  - Authored for device-level execution and NOT executed: every criterion below states what
 *    this module asserts, not an observed result. README.txt.txt records the deferred status
 *    and the prerequisites that are unavailable.
 *
 * @dependencies
 *  - utils.py - send_curl_command, send_vcomponent_command, HDMICEC_CMD_BASE and the logging
 *    helpers.
 *  - HdmiCECSource_Curl.py - the JSON-RPC request constants this module dispatches.
 *  - SuitManager.py - the runner that registers this module and calls run_test().
 *  - vcomponent_configurations/commands/Device_Add.yaml
 *  - vcomponent_configurations/commands/Device_Status.yaml
 *
 * @expected_result
 *  - Both posts are accepted, all three JSON-RPC requests answer, and the final
 *    getActiveSourceStatus reports success true with status true.
 *
 * @pass_criteria
 *  - Both posts accepted, the baseline read, the OTP request and the final read each return a
 *    non-empty response, the final reply reports success True and status True, and run_test()
 *    returns True.
 *
 * @failure_criteria
 *  - Either post is rejected; any of the three requests returns nothing; the final reply
 *    reports success other than True or status other than True; or the final reply does not
 *    parse.
 */
"""


import json
import time
import os
from utils import (
    send_curl_command,
        send_vcomponent_command,
        HDMICEC_CMD_BASE,
        log_info,
        log_success,
        log_error,
        log_warning,
    log_with_timing
)
import HdmiCECSource_Curl as HdmiCecSourceApis


def _post_hdmicec(yaml_file):
    http_code, body = send_vcomponent_command(f"{HDMICEC_CMD_BASE}/{yaml_file}")
    log_info(f"  vComponent POST {yaml_file}: HTTP {http_code}  {body}")
    return http_code == 200


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: active source status after path/routing style changes.
    before = send_curl_command(HdmiCecSourceApis.get_active_source_status)
    if not before:
        log_error("✖ initial getActiveSourceStatus command not sent")
        return False
    log_warning(f"Initial status: {before}")

    ok1 = _post_hdmicec("Device_Add.yaml")
    time.sleep(1)
    ok2 = _post_hdmicec("Device_Status.yaml")
    time.sleep(1)

    if not (ok1 and ok2):
        log_error("✖ required vComponent emulation posts failed")
        return False

    otp = send_curl_command(HdmiCecSourceApis.perform_otp_action)
    if not otp:
        log_error("✖ performOTPAction command not sent")
        return False

    after = send_curl_command(HdmiCecSourceApis.get_active_source_status)
    if not after:
        log_error("✖ final getActiveSourceStatus command not sent")
        return False
    log_warning(f"Final status: {after}")

    try:
        before_body = json.loads(before)
        _ = before_body.get("result", {}).get("status")

        body = json.loads(after)
        result = body.get("result", {})
        if result.get("success") is True and result.get("status") is True:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID27_Active_Source_Transition_After_OTP Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except json.JSONDecodeError:
        pass

    log_error(f"TCID27_Active_Source_Transition_After_OTP Failed ❌")
    return False
