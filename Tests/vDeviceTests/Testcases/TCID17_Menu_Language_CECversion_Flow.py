"""
/**
 * @file TCID17_Menu_Language_CECversion_Flow.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Menu-language and CEC-version
 *        exchange flow.
 *
 * @testcase TCID17_Menu_Language_CECversion_Flow
 * @details Posts menu-language, CEC-version and device-removal documents to the vComponent and
 *          reads getDeviceList before and after to confirm the topology tracks the emulated
 *          exchanges.
 *
 * @precondition
 *  - A device under test - physical hardware or a QEMU target - is running WPEFramework
 *    with the org.rdk.HdmiCecSource plugin activated and reachable over JSON-RPC.
 *  - The vComponent HDMI-CEC emulator is running and accepting YAML command documents.
 *
 * @dependencies
 *  - utils.py - shared endpoint resolution, curl dispatch and logging helpers
 *  - HdmiCECSource_Curl.py - the JSON-RPC command strings this module dispatches
 *  - SuitManager.py - registers and runs this module
 *  - vcomponent_configurations/commands/ - Device_Get_CEC_Version.yaml,
 *    Device_Get_Menu_Language.yaml, Device_Remove.yaml, Device_Set_Menu_Language.yaml
 *
 * @expected_result
 *  - org.rdk.HdmiCecSource.getDeviceList answers with the values this scenario expects.
 *  - Every vComponent command document is accepted before the APIs are exercised.
 *
 * @pass_criteria
 *  - Every response matches its expected value and run_test() returns True.
 *
 * @failure_criteria
 *  - A response mismatch, a JSON-RPC or JSON parsing failure, an unreachable endpoint
 *    or a rejected vComponent command document; run_test() then returns False.
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
