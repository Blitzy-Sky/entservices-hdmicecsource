"""
/**
 * @file TCID28_Invalid_VendorID_Nochange.py
 * @brief L3 HDMI CEC Source functional testcase.
 *
 * @testcase TCID28_Invalid_VendorID_Nochange
 * @details Establishes a baseline with a valid setVendorId followed by getVendorId, sends the
 *          malformed request HdmiCECSource_Curl.set_vendor_id_invalid, and reads getVendorId
 *          again. The malformation is a misspelled parameter key - the params object carries
 *          "vllendorid" instead of "vendorid" - so the plugin receives no vendor id at all
 *          rather than an out-of-range one.
 *
 *          THE VERDICT IS THAT THE VALUE DID NOT MOVE: both the baseline and the final reply
 *          must carry a "result" member and their result.vendorid values must be equal. The
 *          value itself is not pinned, so the case holds on a target provisioned with any
 *          vendor id.
 *
 *          Only the final read is explicitly required to be non-empty. An empty baseline
 *          surfaces as an exception while parsing, which the case catches and turns into a
 *          failure rather than letting it escape run_test().
 *
 * @precondition
 *  - The org.rdk.HdmiCecSource plugin is active and reachable at the JSON-RPC endpoint;
 *    SuitManager activates it with Controller.1.activate before the first case runs.
 *  - None beyond the plugin being reachable; the baseline this case compares against is
 *    written by the case itself.
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
 *  - The malformed request leaves the vendor id unchanged: the baseline and final replies
 *    report the same result.vendorid.
 *
 * @pass_criteria
 *  - The final read returns a non-empty response, both replies carry a result member, the two
 *    vendorid values are equal, and run_test() returns True.
 *
 * @failure_criteria
 *  - The final read returns nothing; either reply lacks a result member; the two vendorid
 *    values differ; or parsing raises.
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
