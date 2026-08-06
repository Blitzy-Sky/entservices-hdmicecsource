"""
/**
 * @file TCID02_Get_Devicelist.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. Reads the discovered CEC device
 *        list.
 *
 * @testcase TCID02_Get_Devicelist
 * @details Invokes getDeviceList and checks the reported numberofdevices and deviceList payload
 *          are well formed.
 *
 * @precondition
 *  - A device under test - physical hardware or a QEMU target - is running WPEFramework
 *    with the org.rdk.HdmiCecSource plugin activated and reachable over JSON-RPC.
 *
 * @dependencies
 *  - utils.py - shared endpoint resolution, curl dispatch and logging helpers
 *  - HdmiCECSource_Curl.py - the JSON-RPC command strings this module dispatches
 *  - SuitManager.py - registers and runs this module
 *
 * @expected_result
 *  - org.rdk.HdmiCecSource.getDeviceList answers with the values this scenario expects.
 *
 * @pass_criteria
 *  - Every response matches its expected value and run_test() returns True.
 *
 * @failure_criteria
 *  - A response mismatch, a JSON-RPC or JSON parsing failure, an unreachable endpoint
 *    or an unavailable device-level prerequisite; run_test() then returns False.
 */
"""


import time
import os


import json
from utils import (
    send_curl_command,
        log_info,
        log_success,
        log_error,
        log_warning,
    log_with_timing
)
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    log_info("Executing the curl command get device list")

    curl_response = send_curl_command(
        HdmiCecSourceApis.get_device_list
    )

    if not curl_response:
        log_error("✖ curl command not sent")
        return False

    log_success("✔ curl command sent")
    log_warning(f"Response: {curl_response}")

    try:
        actual_output_response = json.loads(curl_response)
        result = actual_output_response.get("result", {})
        has_success = result.get("success") is True
        has_count = isinstance(result.get("numberofdevices"), int)

        count = result.get("numberofdevices")
        device_list = result.get("deviceList")
        if count == 0:
            list_consistent = (device_list is None) or (
                isinstance(device_list, list) and len(device_list) == 0
            )
        else:
            # Some targets report count excluding placeholder/NA devices, so
            # the list length can be greater than numberofdevices.
            list_consistent = isinstance(device_list, list) and len(device_list) >= count

        entries_valid = True
        if isinstance(device_list, list):
            for dev in device_list:
                if not isinstance(dev, dict):
                    entries_valid = False
                    break
                if not isinstance(dev.get("logicalAddress"), int):
                    entries_valid = False
                    break

        if has_success and has_count and list_consistent and entries_valid:
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID02_Get_Devicelist Passed ✅"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True

        log_warning(
            f"Actual  : {json.dumps(actual_output_response, indent=2, sort_keys=True)}"
        )
        log_error(f"TCID02_Get_Devicelist Failed ❌")
        return False
    except json.JSONDecodeError:
        log_error("Invalid JSON response")
        log_error(f"TCID02_Get_Devicelist Failed ❌")
        return False
