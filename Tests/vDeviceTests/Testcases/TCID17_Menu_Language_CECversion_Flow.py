"""
/**
 * @file TCID17_Menu_Language_CECversion_Flow.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID17_Menu_Language_CECversion_Flow
 * @details Exercises the menu-language and CEC-version request handlers and then a device
 *          removal. Device_Get_Menu_Language.yaml, Device_Set_Menu_Language.yaml and
 *          Device_Get_CEC_Version.yaml are posted with two-second settle windows;
 *          org.rdk.HdmiCecSource.getDeviceList is then read twice; Device_Remove.yaml is
 *          posted; and getDeviceList is read twice more.
 *
 *          The paired reads are a SETTLING AND LOGGING device, not a comparison: nothing is
 *          diffed between the two reads of a pair, and the inventory before the removal is not
 *          compared with the inventory after it. Each read's body is logged.
 *
 *          The vComponent posts here are ADVISORY: _post_hdmicec() returns whether the API
 *          answered HTTP 200 and the case logs that result without consulting it, so a
 *          rejected document does not fail the case. A stricter contract would assert each
 *          post and each reply body; the criteria below describe what this module enforces,
 *          not what a stricter version could.
 *
 *          THE VERDICT IS THAT ALL FOUR getDeviceList REQUESTS ANSWERED. Position 02 is the
 *          case that validates the inventory's shape, and position 21 is the case that pairs
 *          an add with a remove.
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
 *  - vcomponent_configurations/commands/Device_Get_Menu_Language.yaml
 *  - vcomponent_configurations/commands/Device_Set_Menu_Language.yaml
 *  - vcomponent_configurations/commands/Device_Get_CEC_Version.yaml
 *  - vcomponent_configurations/commands/Device_Remove.yaml
 *
 * @expected_result
 *  - The four documents are accepted (logged) and each of the four getDeviceList reads returns
 *    a response.
 *
 * @pass_criteria
 *  - All four getDeviceList requests return a non-empty response and run_test() returns True.
 *
 * @failure_criteria
 *  - Any of the four getDeviceList requests returns nothing, so run_test() returns False. A
 *    rejected vComponent post does not fail this position.
 */
"""


import time
import os
import subprocess
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
    base_dir = "/tmp"
    time.sleep(2)
    _post_hdmicec("Device_Get_Menu_Language.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Set_Menu_Language.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Get_CEC_Version.yaml")

    for i in range(2):
        time.sleep(1)
        curl_response = send_curl_command(
            HdmiCecSourceApis.get_device_list
        )

        if not curl_response:
            log_error("✖ curl command not sent")
            return False
        else:
            log_warning(f"Response: {curl_response}")


    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    time.sleep(2)
    _post_hdmicec("Device_Remove.yaml")

    for i in range(2):
        time.sleep(1)
        curl_response = send_curl_command(
                HdmiCecSourceApis.get_device_list
            )

        if not curl_response:
            log_error("✖ curl command not sent")
            return False
        else:
            log_warning(f"Response: {curl_response}")

    
    elapsed_time = time.perf_counter() - start_time
    msg = "All commands executed successfully"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
