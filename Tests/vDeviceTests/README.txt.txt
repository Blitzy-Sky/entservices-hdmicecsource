To execute the cases inside qemu

cd /tmp

git clone git@github.com:rdkcentral/entservices-hdmicecsource.git

cd entservices-hdmicecsource/Tests/vDeviceTests

EXECUTION:
with time : python3 SuitManager.py -t hdmicecsource
without time: python3 SuitManager.py hdmicecsource

Default Actions:
Plugin activation is now done by default before suite execution:
- hdmicecsource -> Controller.1.activate(callsign=org.rdk.HdmiCecSource)
- Init_Devicelist_Populate will be done.

Disable default activation only if needed:
- export AUTO_ACTIVATE_PLUGINS=0


If the testcases fail with "connection refused", configure endpoint host/ports before running.

Defaults used by the tests:
- MW JSON-RPC: http://127.0.0.1:9998/jsonrpc
- vComponent API: http://127.0.0.1:8080/api/postKVP

Useful overrides:
- TARGET_HOST (applies to both endpoints)
- JSONRPC_PORT
- VCOMPONENT_PORT
- WPEFRAMEWORK_JSONRPC_URL (full URL, highest priority)
- VCOMPONENT_API_URL (full URL, highest priority)

Examples:

# when running directly inside QEMU guest (services on localhost)
python3 SuitManager.py hdmicecsource

# when running from host against QEMU target IP
export TARGET_HOST=192.168.1.50
export JSONRPC_PORT=9998
export VCOMPONENT_PORT=8080
python3 SuitManager.py hdmicecsource

# full URL override form
export WPEFRAMEWORK_JSONRPC_URL=http://192.168.1.50:9998/jsonrpc
export VCOMPONENT_API_URL=http://192.168.1.50:8080/api/postKVP
python3 SuitManager.py hdmicecsource


Troubleshooting:
- If you see connection errors, verify WPEFramework JSON-RPC and the vComponent API are reachable using the endpoint overrides above.
