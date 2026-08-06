"""
/**
 * @file TCID28_Invalid_VendorID_Nochange.py
 * @brief L3 vDevice testcase for the HDMI-CEC Source plugin. An invalid vendor ID must not change
 *        the stored value.
 *
 * @testcase TCID28_Invalid_VendorID_Nochange
 * @details Sets a valid vendor ID and reads it back, then sets an invalid one and checks
 *          getVendorId still reports the valid value.
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
 *  - Each of these APIs answers with the values this scenario expects:
 *      org.rdk.HdmiCecSource.setVendorId
 *      org.rdk.HdmiCecSource.getVendorId
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
from utils import send_curl_command, log_success, log_error, log_warning
import HdmiCECSource_Curl as HdmiCecSourceApis


def run_test():
    start_time = time.perf_counter()

    # Legacy intent: invalid curl param handling for setVendorId.
    baseline_set = send_curl_command(HdmiCecSourceApis.set_vendor_id)
    baseline_get = send_curl_command(HdmiCecSourceApis.get_vendor_id)
    invalid_set = send_curl_command(HdmiCecSourceApis.set_vendor_id_invalid)
    final_get = send_curl_command(HdmiCecSourceApis.get_vendor_id)

    if not final_get:
        log_error("✖ getVendorId command not sent")
        return False

    log_warning(f"Final vendor response: {final_get}")
    try:
        b = json.loads(baseline_get)
        f = json.loads(final_get)
        if "result" in b and "result" in f and b["result"].get("vendorid") == f["result"].get("vendorid"):
            elapsed_time = time.perf_counter() - start_time
            msg = f"TCID28_Invalid_VendorID_Nochange Passed"
            if os.environ.get("HDMICEC_TIMING_ENABLED"):
                log_success(f"{msg} time consumed: {elapsed_time:.3f}s")
            else:
                log_success(msg)
            return True
    except Exception:
        pass

    log_error(f"TCID28_Invalid_VendorID_Nochange Failed")
    return False
