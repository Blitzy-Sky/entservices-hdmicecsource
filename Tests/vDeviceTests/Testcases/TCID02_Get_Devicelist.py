"""
/**
 * @file TCID02_Get_Devicelist.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID02_Get_Devicelist
 * @details Calls org.rdk.HdmiCecSource.getDeviceList and validates the SHAPE of the published
 *          inventory rather than a fixed census, so the case is stable whether or not
 *          discovery has completed: result.success is exactly True, result.numberofdevices is
 *          an int, the deviceList is consistent with that count, and every entry is a dict
 *          carrying an int logicalAddress.
 *
 *          Consistency is asymmetric on purpose. When the count is 0 the list may be absent or
 *          an empty list; when it is positive the list must be a list whose length is at least
 *          the count, because some targets exclude placeholder or NA entries from the count
 *          while still publishing them. A list longer than the count is therefore not a
 *          failure.
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - No specific peer is required. Init_Devicelist_Populate normally seeds six devices before
 *    the suite starts, but none of the assertions here depends on any of them.
 *  - Authored for device-level execution and NOT executed: every criterion below states what
 *    this module asserts, not an observed result. README.txt.txt records the deferred status
 *    and the prerequisites that are unavailable.
 *
 * @dependencies
 *  - utils.py - send_curl_command and the logging helpers.
 *  - HdmiCECSource_Curl.py - the JSON-RPC request constants this module dispatches.
 *  - SuitManager.py - the runner that registers this module and calls run_test().
 *
 * @expected_result
 *  - success true, an integer device count, a deviceList consistent with that count, and
 *    entries whose logicalAddress members are integers.
 *
 * @pass_criteria
 *  - All four predicates hold - success is True, the count is an int, the list is consistent
 *    with the count, and every entry validates - and run_test() returns True.
 *
 * @failure_criteria
 *  - Empty response; success not True; the count not an int; a positive count with a non-list
 *    or a shorter list; an entry that is not a dict or whose logicalAddress is not an int; or
 *    a JSONDecodeError.
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
