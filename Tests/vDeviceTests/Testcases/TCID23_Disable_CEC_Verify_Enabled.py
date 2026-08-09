"""
/**
 * @file TCID23_Disable_CEC_Verify_Enabled.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID23_Disable_CEC_Verify_Enabled
 * @details Disables CEC, confirms the reader answers, then tries to re-enable it while the
 *          emulated HAL open call is forced to fail. setEnabled(enabled=false) is sent;
 *          getEnabled is read; Device_Setapi_Open_Fail.yaml is posted;
 *          setEnabled(enabled=true) is sent against the failing HAL; and
 *          Device_Setapi_Open_Pass.yaml is posted as a POST-CONDITION.
 *
 *          BOTH POSTS ARE REQUIRED - a rejection fails the case. The closing
 *          Device_Setapi_Open_Pass.yaml is what clears the injected open failure, both this
 *          position's and the one position 22 leaves behind, which is why it is a
 *          post-condition rather than an advisory step.
 *
 *          The re-enable call sits in a try/except that FAILS the case on an exception. That
 *          is the difference from position 22, which warns and continues: here the enable
 *          attempt under fault is the observation, so an exception is a result rather than
 *          noise.
 *
 *          Every reply body is logged rather than parsed; what is asserted is that each
 *          request answered and that both documents were accepted.
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
 *  - vcomponent_configurations/commands/Device_Setapi_Open_Fail.yaml
 *  - vcomponent_configurations/commands/Device_Setapi_Open_Pass.yaml
 *
 * @expected_result
 *  - Both documents are accepted in order (fault injected, then cleared), and each of the
 *    three JSON-RPC requests - setEnabled(false), getEnabled, setEnabled(true) - returns a
 *    response.
 *
 * @pass_criteria
 *  - Both documents are accepted, all three JSON-RPC requests return a non-empty response, no
 *    exception escapes the enable attempt, and run_test() returns True.
 *
 * @failure_criteria
 *  - Either document is rejected, any of the three requests returns nothing, or the re-enable
 *    attempt raises - in which case the injected fault is left uncleared and run_test()
 *    returns False.
 */
"""


import time
import os
import json
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
    """Post a HdmiCec vComponent YAML command using the new curl API."""
    http_code, body = send_vcomponent_command(f"{HDMICEC_CMD_BASE}/{yaml_file}")
    log_info(f"  vComponent POST {yaml_file}: HTTP {http_code}  {body}")
    return http_code == 200

def run_test():
    start_time = time.perf_counter()

    #base_dir = "/tmp/vcomponent_configurations/commands"
    log_success("Negative scenario - Making the setEnabled driver status as FALSE")
    curl_response = send_curl_command(
            HdmiCecSourceApis.set_enabled_false
        )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False
    else:
        log_warning(f"Response: {curl_response}")

    time.sleep(2)
    log_success("Negative scenario - verifying the driver status with getEnabled")
    curl_response = send_curl_command(
            HdmiCecSourceApis.get_enabled
        )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False
    else:
        log_warning(f"Response: {curl_response}")

    log_error("Overriding the HAL API HdmICecOpen return value as negative")
    time.sleep(3)
    if not _post_hdmicec("Device_Setapi_Open_Fail.yaml"):
        log_error("✖ missing or rejected vComponent YAML: Device_Setapi_Open_Fail.yaml")
        return False
    time.sleep(2)
    try:
        log_success("Negative scenario - making the driver status as TRUE using setEnabled")
        curl_response = send_curl_command(
            HdmiCecSourceApis.set_enabled_true
        )

        if not curl_response:
            log_error("✖ curl command not sent")
            return False
        else:
            log_warning(f"Response: {curl_response}")
    except Exception as exc:
        log_error(f"Exception during negative scenario: {exc}")
        return False
    
    log_error("Overriding the HAL API HdmICecOpen return value as POSITIVE as post condition")
    time.sleep(3)
    if not _post_hdmicec("Device_Setapi_Open_Pass.yaml"):
        log_error("✖ missing or rejected vComponent YAML: Device_Setapi_Open_Pass.yaml")
        return False
    
    elapsed_time = time.perf_counter() - start_time
    msg = "TCID23_Disable_CEC_Verify_Enabled"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True