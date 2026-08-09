"""
/**
 * @file TCID22_Verify_Devicelist_CEC_Disabled.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID22_Verify_Devicelist_CEC_Disabled
 * @details A negative-path case: it drives the emulated HAL into failure and checks that the
 *          plugin keeps answering. org.rdk.HdmiCecSource.getEnabled and getDeviceList are read
 *          for a baseline; Device_Setapi_Open_Fail.yaml and Device_Setapi_Logic_Fail.yaml are
 *          posted; then setEnabled(enabled=true) is sent and getDeviceList is read again while
 *          the HAL open and logical-address calls are returning errors.
 *
 *          THESE TWO POSTS ARE REQUIRED, unlike the advisory posts elsewhere in this suite:
 *          _post_hdmicec()'s result is checked and a rejected document fails the case, because
 *          without the injected fault there is no negative path to exercise.
 *
 *          The second half runs inside a try/except that logs an exception as a WARNING and
 *          continues, so what this position asserts is that the plugin remains responsive
 *          under HAL failure - not that any particular reply is produced. Every reply body is
 *          logged.
 *
 *          THE INJECTED FAULT IS NOT UNDONE HERE. Position 23 posts
 *          Device_Setapi_Open_Pass.yaml, which clears it; a run that stops between the two
 *          leaves the emulated HAL failing.
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
 *  - vcomponent_configurations/commands/Device_Setapi_Logic_Fail.yaml
 *
 * @expected_result
 *  - Both fault-injection documents are accepted, and each of the four JSON-RPC requests -
 *    getEnabled, getDeviceList, setEnabled(true) and getDeviceList - returns a response.
 *
 * @pass_criteria
 *  - Both documents are accepted, all four JSON-RPC requests return a non-empty response, and
 *    run_test() returns True.
 *
 * @failure_criteria
 *  - Either fault-injection document is rejected, or any of the four JSON-RPC requests returns
 *    nothing. An exception raised in the second half is warned about and does not fail the
 *    case.
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
    log_success("Negative scenario - calling getEnabled with driver status TRUE")
    curl_response = send_curl_command(
            HdmiCecSourceApis.get_enabled
        )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False
    else:
        log_warning(f"Response: {curl_response}")

    time.sleep(2)
    log_success("Negative scenario - calling getDeviceList")
    curl_response = send_curl_command(
            HdmiCecSourceApis.get_device_list
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
    if not _post_hdmicec("Device_Setapi_Logic_Fail.yaml"):
        log_error("✖ missing or rejected vComponent YAML: Device_Setapi_Logic_Fail.yaml")
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
        log_success("Negative scenario - calling getDeviceList After setting the HdmiCecLogical address and HdmiCecOpen HAL APIs return value error")
        time.sleep(2)
        curl_response = send_curl_command(
                HdmiCecSourceApis.get_device_list
            )

        if not curl_response:
            log_error("✖ curl command not sent")
            return False
        else:
            log_warning(f"Response: {curl_response}")
    except Exception as exc:
        log_warning(f"Driver FAILED: {exc}")
    
    elapsed_time = time.perf_counter() - start_time
    msg = "TCID22_Verify_Devicelist_CEC_Disabled passed"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True


   
