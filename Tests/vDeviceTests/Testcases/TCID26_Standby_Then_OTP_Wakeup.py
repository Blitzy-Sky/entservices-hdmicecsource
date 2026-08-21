"""
/**
 * @file TCID26_Standby_Then_OTP_Wakeup.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID26_Standby_Then_OTP_Wakeup
 * @details Posts Device_CEC_Message_Userdef.yaml, sends
 *          org.rdk.HdmiCecSource.sendStandbyMessage, posts Device_Status.yaml, then sends
 *          performOTPAction - a standby request followed by a wake-up request with the
 *          emulated peer's state reported in between.
 *
 *          ONLY THE OTP REPLY CARRIES THE VERDICT. _json_success() requires result to be a
 *          dict whose success is exactly True, and it treats any parsing exception as a
 *          failure. The standby reply is required only to be non-empty, and both posts are
 *          advisory.
 *
 *          Neither the standby broadcast nor the peer's power transition is observable through
 *          this transport, so what the case establishes is that the plugin ACCEPTS a
 *          one-time-play walk after a standby request - not that a peer woke up.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - The vComponent API is reachable and serving the emulated CEC peers; HDMICEC_CMD_BASE
 *    resolves to the commands directory this module posts from.
 *  - Authored for device-level execution and NOT executed: every criterion below states what
 *    this module asserts, not an observed result. README.txt.txt records the deferred status
 *    and the prerequisites that are unavailable.
 *
 * @dependencies
 *  - utils.py - send_curl_command, send_vcomponent_command, HDMICEC_CMD_BASE and the logging
 *    helpers.
 *  - HdmiCECSource_Curl.py - the JSON-RPC request constants this module dispatches.
 *  - SuitManager.py - the runner that registers this module and calls run_test().
 *  - vcomponent_configurations/commands/Device_CEC_Message_Userdef.yaml
 *  - vcomponent_configurations/commands/Device_Status.yaml
 *
 * @expected_result
 *  - The standby request answers and the performOTPAction reply carries result.success true.
 *
 * @pass_criteria
 *  - Both requests return a non-empty response, the OTP reply parses with result a dict whose
 *    success is True, and run_test() returns True.
 *
 * @failure_criteria
 *  - Either request returns nothing, or the OTP reply lacks a result dict with success True -
 *    including the case where it does not parse.
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


def _json_success(response):
    try:
        body = json.loads(response)
        return isinstance(body.get("result"), dict) and body["result"].get("success") is True
    except Exception:
        return False


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: standby from standby then OTP wake-up.
    _post_hdmicec("Device_CEC_Message_Userdef.yaml")
    time.sleep(1)

    standby_response = send_curl_command(HdmiCecSourceApis.send_standby_message)
    if not standby_response:
        log_error("✖ standby curl command not sent")
        return False
    log_warning(f"Standby Response: {standby_response}")

    _post_hdmicec("Device_Status.yaml")
    time.sleep(1)

    otp_response = send_curl_command(HdmiCecSourceApis.perform_otp_action)
    if not otp_response:
        log_error("✖ performOTPAction curl command not sent")
        return False
    log_warning(f"OTP Response: {otp_response}")

    if _json_success(otp_response):
        elapsed_time = time.perf_counter() - start_time
        msg = f"TCID26_Standby_Then_OTP_Wakeup Passed"
        if os.environ.get("HDMICEC_TIMING_ENABLED"):
            log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
        else:
            log_success(msg)
        return True

    log_error(f"TCID26_Standby_Then_OTP_Wakeup Failed")
    return False
