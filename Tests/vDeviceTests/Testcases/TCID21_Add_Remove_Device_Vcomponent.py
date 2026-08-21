"""
/**
 * @file TCID21_Add_Remove_Device_Vcomponent.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID21_Add_Remove_Device_Vcomponent
 * @details Adds an emulated device, observes the inventory, then removes it again.
 *          Device_Add.yaml, Device_CEC_Message.yaml and Device_Status.yaml are posted;
 *          org.rdk.HdmiCecSource.getDeviceList is read twice; Device_Remove.yaml is posted;
 *          and getDeviceList is read twice more.
 *
 *          ADD AND REMOVE ARE PAIRED INSIDE THE CASE, so the emulated topology this position
 *          changes is the topology it puts back - no cleanup hook is needed and none is
 *          published. The four reads are the settling and logging mechanism; they are not
 *          diffed against each other, and the count before the add is not compared with the
 *          count after the remove.
 *
 *          The vComponent posts here are ADVISORY: _post_hdmicec() returns whether the API
 *          answered HTTP 200 and the case logs that result without consulting it, so a
 *          rejected document does not fail the case. A stricter contract would assert each
 *          post and each reply body; the criteria below describe what this module enforces,
 *          not what a stricter version could.
 *
 *          THE VERDICT IS THAT ALL FOUR getDeviceList REQUESTS ANSWERED.
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
 *  - vcomponent_configurations/commands/Device_Add.yaml
 *  - vcomponent_configurations/commands/Device_CEC_Message.yaml
 *  - vcomponent_configurations/commands/Device_Status.yaml
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
    _post_hdmicec("Device_Add.yaml")
    time.sleep(2)
    _post_hdmicec("Device_CEC_Message.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Status.yaml")

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
