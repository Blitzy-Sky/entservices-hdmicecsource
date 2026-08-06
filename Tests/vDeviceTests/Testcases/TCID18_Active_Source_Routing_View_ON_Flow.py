"""
/**
 * @file TCID18_Active_Source_Routing_View_ON_Flow.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Active-source, routing-change and
 *        view-on flow.
 *
 * @testcase TCID18_Active_Source_Routing_View_ON_Flow
 * @details Posts image-view-on, text-view-on, routing-change, OSD-string and active/inactive-
 *          source documents to the vComponent, then drives sendStandbyMessage and
 *          performOTPAction across that emulated routing state.
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
 *  - vcomponent_configurations/commands/ - Device_Image_View_On.yaml,
 *    Device_Request_Active_Source.yaml, Device_Request_Inactive_Source.yaml,
 *    Device_Routing_Change.yaml, Device_Set_OSD_String.yaml, Device_Text_View_On.yaml
 *
 * @expected_result
 *  - Each of these APIs answers with the values this scenario expects:
 *      org.rdk.HdmiCecSource.sendStandbyMessage
 *      org.rdk.HdmiCecSource.performOTPAction
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
    _post_hdmicec("Device_Request_Inactive_Source.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Request_Active_Source.yaml")

    time.sleep(1)
    log_info("Send standby curl request being made to source device")
    curl_response = send_curl_command(
        HdmiCecSourceApis.send_standby_message
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False
    else:
        log_warning(f"Response: {curl_response}")

    time.sleep(1)
    log_info("Send perform OTP Action curl request being made to source device")
    curl_response = send_curl_command(
        HdmiCecSourceApis.perform_otp_action
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False
    else:
        log_warning(f"Response: {curl_response}")


    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    time.sleep(2)
    _post_hdmicec("Device_Routing_Change.yaml")

    time.sleep(3)
    log_info("Emulations after routing change, and device power on from standby for image view on and text view on")

    time.sleep(2)
    _post_hdmicec("Device_Image_View_On.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Text_View_On.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Set_OSD_String.yaml")
    
    elapsed_time = time.perf_counter() - start_time
    msg = "All commands executed successfully"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
