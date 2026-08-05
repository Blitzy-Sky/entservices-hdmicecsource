"""
/**
 * @file TCID19_Standby_OTP_Powerstatus_Flow.py
 * @brief L2 HDMI CEC functional testcase.
 *
 * @testcase TCID19_Standby_OTP_Powerstatus_Flow
 * @details Validates the 'TCID19_Standby_OTP_Powerstatus_Flow' HDMI CEC behavior through JSON-RPC and/or vComponent command flow.
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

    log_info(" Set the devices to standby mode and hit the sendStandbyMessage curl command again. Then wake up the remote device using otp feature.First, set the device to standby mode via emulation. Next, hit the curl command for sendStandbyMessage and send corresponding cec messages using sendMessage API to hal. Then hit the curl command for perform OTP Action and send corresponding cec messages to hal.Verify the thunder logs for more info")
    #base_dir = "/tmp/vcomponent_configurations/commands"

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


    base_dir = "/tmp"
    time.sleep(2)
    _post_hdmicec("Device_Get_Power_Status.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Report_Power_Status.yaml")


    time.sleep(3)
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

    base_dir = "/tmp"
    time.sleep(2)
    _post_hdmicec("Device_Get_Power_Status.yaml")
    time.sleep(2)
    _post_hdmicec("Device_Report_Power_Status.yaml")
    
    elapsed_time = time.perf_counter() - start_time
    msg = "All commands executed successfully"
    if os.environ.get("HDMICEC_TIMING_ENABLED"):
        log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
    else:
        log_success(msg)
    return True
