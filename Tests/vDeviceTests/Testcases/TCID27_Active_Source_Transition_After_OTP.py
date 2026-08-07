"""
/**
 * @file TCID27_Active_Source_Transition_After_OTP.py
 * @brief L2 HDMI CEC functional testcase.
 *
 * @testcase TCID27_Active_Source_Transition_After_OTP
 * @details Validates the 'TCID27_Active_Source_Transition_After_OTP' HDMI CEC behavior through JSON-RPC and/or vComponent command flow.
 *
 * @precondition
 *  - Required plugin is active and reachable via JSON-RPC endpoint.
 *  - Target environment is ready for HDMI CEC emulation/command execution.
 *
 * @dependencies
 *  - utils.py
 *  - HdmiCECSource_Curl.py
 *  - SuitManager.py
 *  - vcomponent_configurations/hdmicec/commands/*.yaml (for emulation-based scenarios)
 *
 * @expected_result
 *  - API responses and scenario validations match expected values.
 *
 * @pass_criteria
 *  - Expected response equals actual response and testcase returns True.
 *
 * @failure_criteria
 *  - Response mismatch, command failure, JSON parsing error, or testcase returns False.
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
