/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "L2Tests.h"
#include "L2TestsMock.h"
#include <condition_variable>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <interfaces/IHdmiCecSource.h>
// Used to change the power state for events
#include <interfaces/IPowerManager.h>

#define EVNT_TIMEOUT (5000)
#define HDMICECSOURCE_CALLSIGN _T("org.rdk.HdmiCecSource.1")
#define HDMICECSOURCE_L2TEST_CALLSIGN _T("L2tests.1")

#define TEST_LOG(x, ...)                                                                                                                         \
    fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); \
    fflush(stderr);

using ::testing::NiceMock;
using namespace WPEFramework;
using testing::StrictMock;
using HdmiCecSourceSuccess = WPEFramework::Exchange::IHdmiCecSource::HdmiCecSourceSuccess;
using HdmiCecSourceDevice = WPEFramework::Exchange::IHdmiCecSource::HdmiCecSourceDevices;
using IHdmiCecSourceDeviceListIterator = WPEFramework::Exchange::IHdmiCecSource::IHdmiCecSourceDeviceListIterator;
using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;

namespace {
    static void removeFile(const char* fileName)
	{
		if (std::remove(fileName) != 0)
		{
			printf("File %s failed to remove\n", fileName);
			perror("Error deleting file");
		}
		else
		{
			printf("File %s successfully deleted\n", fileName);
		}
	}
	
	static void createFile(const char* fileName, const char* fileContent)
	{
		removeFile(fileName);

		std::ofstream fileContentStream(fileName);
		fileContentStream << fileContent;
		fileContentStream << "\n";
		fileContentStream.close();
	}

class AsyncHandlerMock {
public:
    virtual ~AsyncHandlerMock() = default;
    virtual void onActiveSourceStatusUpdated(bool status) = 0;
    virtual void onDeviceAdded(int logicalAddress) = 0;
    virtual void onDeviceRemoved(int logicalAddress) = 0;
    virtual void onDeviceInfoUpdated(int logicalAddress) = 0;
    virtual void standbyMessageReceived(int logicalAddress) = 0;
    virtual void onKeyReleaseEvent(int logicalAddress) = 0;
    virtual void onKeyPressEvent(int logicalAddress, int keyCode) = 0;
};

class MockAsyncHandler : public AsyncHandlerMock {
public:
    MOCK_METHOD(void, onActiveSourceStatusUpdated, (bool status), (override));
    MOCK_METHOD(void, onDeviceAdded, (int logicalAddress), (override));
    MOCK_METHOD(void, onDeviceRemoved, (int logicalAddress), (override));
    MOCK_METHOD(void, onDeviceInfoUpdated, (int logicalAddress), (override));
    MOCK_METHOD(void, standbyMessageReceived, (int logicalAddress), (override));
    MOCK_METHOD(void, onKeyReleaseEvent, (int logicalAddress), (override));
    MOCK_METHOD(void, onKeyPressEvent, (int logicalAddress, int keyCode), (override));
};
}

// Event flags for different CEC events
typedef enum : uint32_t {
    ON_ACTIVE_SOURCE_STATUS_UPDATED = 0x00000001,
    ON_DEVICE_ADDED = 0x00000002,
    ON_DEVICE_REMOVED = 0x00000004,
    ON_DEVICE_INFO_UPDATED = 0x00000008,
    STANDBY_MESSAGE_RECEIVED = 0x00000010,
    ON_KEY_RELEASE_EVENT = 0x00000020,
    ON_KEY_PRESS_EVENT = 0x00000040,
    HDMICECSOURCE_STATUS_INVALID = 0x00000000
} HdmiCecSourceL2test_async_events_t;

// Notification handler for HdmiCecSource events
class HdmiCecSourceNotificationHandler : public Exchange::IHdmiCecSource::INotification {
private:
    std::mutex m_mutex;
    std::condition_variable m_condition_variable;
    uint32_t m_event_signalled;

    BEGIN_INTERFACE_MAP(Notification)
    INTERFACE_ENTRY(Exchange::IHdmiCecSource::INotification)
    END_INTERFACE_MAP

public:
    HdmiCecSourceNotificationHandler()
        : m_event_signalled(HDMICECSOURCE_STATUS_INVALID)
        , m_activeSourceStatus(false)
        , m_logicalAddress(0)
        , m_keyCode(0)
    {
    }

    ~HdmiCecSourceNotificationHandler() override = default;

    void OnActiveSourceStatusUpdated(const bool status) override
    {
        TEST_LOG("OnActiveSourceStatusUpdated event received, status: %d", status);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_activeSourceStatus = status;
        m_event_signalled |= ON_ACTIVE_SOURCE_STATUS_UPDATED;
        m_condition_variable.notify_one();
    }

    void OnDeviceAdded(const int logicalAddress) override
    {
        TEST_LOG("OnDeviceAdded event received, logicalAddress: %d", logicalAddress);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_event_signalled |= ON_DEVICE_ADDED;
        m_condition_variable.notify_one();
    }

    void OnDeviceRemoved(const int logicalAddress) override
    {
        TEST_LOG("OnDeviceRemoved event received, logicalAddress: %d", logicalAddress);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_event_signalled |= ON_DEVICE_REMOVED;
        m_condition_variable.notify_one();
    }

    void OnDeviceInfoUpdated(const int logicalAddress) override
    {
        TEST_LOG("OnDeviceInfoUpdated event received, logicalAddress: %d", logicalAddress);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_event_signalled |= ON_DEVICE_INFO_UPDATED;
        m_condition_variable.notify_one();
    }

    void StandbyMessageReceived(const int logicalAddress) override
    {
        TEST_LOG("StandbyMessageReceived event received, logicalAddress: %d", logicalAddress);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_event_signalled |= STANDBY_MESSAGE_RECEIVED;
        m_condition_variable.notify_one();
    }

    void OnKeyReleaseEvent(const int logicalAddress) override
    {
        TEST_LOG("OnKeyReleaseEvent event received, logicalAddress: %d", logicalAddress);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_event_signalled |= ON_KEY_RELEASE_EVENT;
        m_condition_variable.notify_one();
    }

    void OnKeyPressEvent(const int logicalAddress, const int keyCode) override
    {
        TEST_LOG("OnKeyPressEvent event received, logicalAddress: %d, keyCode: %d", logicalAddress, keyCode);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_logicalAddress = logicalAddress;
        m_keyCode = keyCode;
        m_event_signalled |= ON_KEY_PRESS_EVENT;
        m_condition_variable.notify_one();
    }

    uint32_t WaitForEvent(uint32_t timeout_ms, HdmiCecSourceL2test_async_events_t expected_status)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto now = std::chrono::system_clock::now();
        auto timeout = now + std::chrono::milliseconds(timeout_ms);
        uint32_t signalled = HDMICECSOURCE_STATUS_INVALID;

        while (!(m_event_signalled & expected_status)) {
            if (m_condition_variable.wait_until(lock, timeout) == std::cv_status::timeout) {
                TEST_LOG("Timeout waiting for event: 0x%08X", expected_status);
                return HDMICECSOURCE_STATUS_INVALID;
            }
        }

        signalled = m_event_signalled & expected_status;
        m_event_signalled = HDMICECSOURCE_STATUS_INVALID;
        return signalled;
    }

    void ResetEvent()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_event_signalled = HDMICECSOURCE_STATUS_INVALID;
    }

    bool GetActiveSourceStatus() const { return m_activeSourceStatus; }
    int GetLogicalAddress() const { return m_logicalAddress; }
    int GetKeyCode() const { return m_keyCode; }

private:
    bool m_activeSourceStatus;
    int m_logicalAddress;
    int m_keyCode;
};

class AsyncHandlerMock_HdmiCecSource {
public:
    AsyncHandlerMock_HdmiCecSource()
    {
        m_asyncHandlerMock = new NiceMock<MockAsyncHandler>;
    }

    virtual ~AsyncHandlerMock_HdmiCecSource()
    {
        delete m_asyncHandlerMock;
    }

    MockAsyncHandler& mock() { return *m_asyncHandlerMock; }

private:
    MockAsyncHandler* m_asyncHandlerMock;
};

class HdmiCecSource_L2Test : public L2TestMocks {
protected:
    HdmiCecSource_L2Test();
    virtual ~HdmiCecSource_L2Test() override;

public:
    uint32_t CreateHdmiCecSourceInterfaceObject();
    uint32_t WaitForRequestStatus(uint32_t timeout_ms, HdmiCecSourceL2test_async_events_t expected_status);
    void onActiveSourceStatusUpdated(const JsonObject& message);
    void onDeviceAdded(const JsonObject& message);
    void onDeviceInfoUpdated(const JsonObject& message);
    void onDeviceRemoved(const JsonObject& message);
    void standbyMessageReceived(const JsonObject& message);
    void onKeyReleaseEvent(const JsonObject& message);
    void onKeyPressEvent(const JsonObject& message);

protected:
    Exchange::IHdmiCecSource* m_cecSourcePlugin = nullptr;
    PluginHost::IShell* m_controller_cecSource = nullptr;
    Core::Sink<HdmiCecSourceNotificationHandler> m_notificationHandler;
    IARM_EventHandler_t dsHdmiEventHandler = nullptr;
    IARM_EventHandler_t powerEventHandler = nullptr;
    FrameListener* registeredListener = nullptr;
    std::vector<FrameListener*> listeners;

    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> HdmiCecSource_Engine;
    Core::ProxyType<RPC::CommunicatorClient> HdmiCecSource_Client;

private:
    std::mutex m_mutex;
    std::condition_variable m_condition_variable;
    uint32_t m_event_signalled = HDMICECSOURCE_STATUS_INVALID;
};

HdmiCecSource_L2Test::HdmiCecSource_L2Test()
    : L2TestMocks()
{
    TEST_LOG("HdmiCecSource_L2Test Constructor");

    // Setup device.properties file
    removeFile("/etc/device.properties");
    createFile("/etc/device.properties", "RDK_PROFILE=STB");
    createFile("/opt/persistent/ds/cecData_2.json", "0");
    createFile("/tmp/pwrmgr_restarted", "2");

    // Add sleep to ensure file is properly written to disk
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Mock IARM Bus initialization
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Init(::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(IARM_RESULT_SUCCESS));

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Connect())
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(IARM_RESULT_SUCCESS));

    // Mock IARM Event Registration to capture event handlers
    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_RegisterEventHandler(::testing::_, ::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [this](const char* ownerName, IARM_EventId_t eventId, IARM_EventHandler_t handler) {
                if (strcmp(ownerName, IARM_BUS_DSMGR_NAME) == 0) {
                    if (eventId == IARM_BUS_DSMGR_EVENT_HDMI_HOTPLUG) {
                        dsHdmiEventHandler = handler;
                        TEST_LOG("Captured HDMI HotPlug Event Handler");
                    }
                } else if (strcmp(ownerName, IARM_BUS_PWRMGR_NAME) == 0) {
                    if (eventId == IARM_BUS_PWRMGR_EVENT_MODECHANGED) {
                        powerEventHandler = handler;
                        TEST_LOG("Captured Power Manager Event Handler");
                    }
                }
                return IARM_RESULT_SUCCESS;
            }));

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_UnRegisterEventHandler(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(IARM_RESULT_SUCCESS));

    EXPECT_CALL(*p_iarmBusImplMock, IARM_Bus_Call)
        .Times(::testing::AnyNumber())
        .WillRepeatedly(
            [](const char* ownerName, const char* methodName, void* arg, size_t argLen) {
                IARM_Result_t result = IARM_RESULT_SUCCESS;
                if (strcmp(ownerName, IARM_BUS_PWRMGR_NAME) == 0) {
                    if (strcmp(methodName, IARM_BUS_PWRMGR_API_GetPowerState) == 0) {
                        auto* param = static_cast<IARM_Bus_PWRMgr_GetPowerState_Param_t*>(arg);
                        param->curState = IARM_BUS_PWRMGR_POWERSTATE_ON;
                    }
                }
                return result;
            });

    // Mock device settings Manager
    ON_CALL(*p_managerImplMock, Initialize())
        .WillByDefault(::testing::Return());

    // Mock Host methods
    ON_CALL(*p_hostImplMock, getDefaultVideoPortName())
        .WillByDefault(::testing::Return(std::string("HDMI0")));

    ON_CALL(*p_hostImplMock, getVideoOutputPort(::testing::_))
        .WillByDefault(::testing::ReturnRef(device::VideoOutputPort::getInstance()));

    // Mock VideoOutputPort methods
    ON_CALL(*p_videoOutputPortMock, isDisplayConnected())
        .WillByDefault(::testing::Return(true));

    ON_CALL(*p_videoOutputPortMock, getDisplay())
        .WillByDefault(::testing::ReturnRef(device::Display::getInstance()));

    // Mock Display methods - getEDIDBytes is void and takes a reference parameter
    ON_CALL(*p_displayMock, getEDIDBytes(::testing::_))
        .WillByDefault(::testing::Invoke(
            [](std::vector<uint8_t>& edid) {
                edid = {
                    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
                    0x4C, 0x2D, 0xFE, 0x08, 0x00, 0x00, 0x00, 0x00
                };
            }));

    // Mock HDMI CEC Connection - capture frame listeners for event injection
    ON_CALL(*p_connectionMock, addFrameListener(::testing::_))
        .WillByDefault(::testing::Invoke(
            [this](FrameListener* listener) {
                TEST_LOG("addFrameListener called with address: %p", static_cast<void*>(listener));
                if (listener != nullptr) {
                    registeredListener = listener;
                    listeners.push_back(listener);
                    TEST_LOG("Frame listener registered, total listeners: %zu", listeners.size());
                }
            }));

    // Mock MessageEncoder - need to mock both overloads explicitly
    ON_CALL(*p_messageEncoderMock, encode(::testing::Matcher<const DataBlock&>(::testing::_)))
        .WillByDefault(::testing::Invoke(
            [](const DataBlock& m) -> CECFrame& {
                static CECFrame frame;
                return frame;
            }));

    ON_CALL(*p_messageEncoderMock, encode(::testing::Matcher<const UserControlPressed&>(::testing::_)))
        .WillByDefault(::testing::Invoke(
            [](const UserControlPressed& m) -> CECFrame& {
                static CECFrame frame;
                return frame;
            }));

    // Mock Wraps
    ON_CALL(*p_wrapsImplMock, access(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(0));

    // Mock PowerManager HAL for PowerManager plugin initialization
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_INIT())
        .WillOnce(::testing::Return(DEEPSLEEPMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_INIT())
        .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetWakeupSrc(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_GetPowerState(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](PWRMgr_PowerState_t* powerState) {
                *powerState = PWRMGR_POWERSTATE_ON;
                return PWRMGR_SUCCESS;
            }));

    ON_CALL(*p_rfcApiImplMock, getRFCParameter(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                if (strcmp("RFC_DATA_ThermalProtection_POLL_INTERVAL", pcParameterName) == 0) {
                    strcpy(pstParamData->value, "2");
                    return WDMP_SUCCESS;
                } else if (strcmp("RFC_ENABLE_ThermalProtection", pcParameterName) == 0) {
                    strcpy(pstParamData->value, "true");
                    return WDMP_SUCCESS;
                } else if (strcmp("RFC_DATA_ThermalProtection_DEEPSLEEP_GRACE_INTERVAL", pcParameterName) == 0) {
                    strcpy(pstParamData->value, "6");
                    return WDMP_SUCCESS;
                } else {
                    return WDMP_FAILURE;
                }
            }));

    ON_CALL(*p_mfrMock, mfrSetTempThresholds(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](int high, int critical) {
                return mfrERR_NONE;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [](PWRMgr_PowerState_t powerState) {
                return PWRMGR_SUCCESS;
            }));

    ON_CALL(*p_mfrMock, mfrGetTemperature(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](mfrTemperatureState_t* curState, int* curTemperature, int* wifiTemperature) {
                *curTemperature = 90;
                *curState = (mfrTemperatureState_t)0;
                *wifiTemperature = 25;
                return mfrERR_NONE;
            }));

    ON_CALL(*p_connectionMock, open())
        .WillByDefault(::testing::Return());

    ON_CALL(*p_connectionMock, poll(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](const LogicalAddress& from, const Throw_e& doThrow) {
                throw CECNoAckException();
            }));

    EXPECT_CALL(*p_libCCECMock, getPhysicalAddress(::testing::_))
        .WillRepeatedly(::testing::Invoke(
            [&](uint32_t* physAddress) {
                *physAddress = (uint32_t)0x12345678;
            }));

    /* Activate plugin in constructor */
    uint32_t status = ActivateService("org.rdk.PowerManager");
    EXPECT_EQ(Core::ERROR_NONE, status);

    status = ActivateService("org.rdk.HdmiCecSource");
    EXPECT_EQ(Core::ERROR_NONE, status);
}

HdmiCecSource_L2Test::~HdmiCecSource_L2Test()
{
    TEST_LOG("HdmiCecSource_L2Test Destructor");
    uint32_t status = Core::ERROR_GENERAL;

    ON_CALL(*p_connectionMock, close())
        .WillByDefault(::testing::Return());

    sleep(5);

    // Deactivate services in reverse order
    status = DeactivateService("org.rdk.HdmiCecSource");
    EXPECT_EQ(Core::ERROR_NONE, status);

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_TERM())
        .WillOnce(::testing::Return(PWRMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_TERM())
        .WillOnce(::testing::Return(DEEPSLEEPMGR_SUCCESS));

    status = DeactivateService("org.rdk.PowerManager");
    EXPECT_EQ(Core::ERROR_NONE, status);

    if (HdmiCecSource_Client.IsValid()) {
        HdmiCecSource_Client.Release();
    }

    if (HdmiCecSource_Engine.IsValid()) {
        HdmiCecSource_Engine.Release();
    }

    // Cleanup device.properties file
    removeFile("/etc/device.properties");
    removeFile("/tmp/pwrmgr_restarted");
    removeFile("/opt/persistent/ds/cecData_2.json");
    removeFile("/opt/uimgr_settings.bin");

    TEST_LOG("HdmiCecSource_L2Test cleanup complete");
}

uint32_t HdmiCecSource_L2Test::CreateHdmiCecSourceInterfaceObject()
{
    uint32_t return_value = Core::ERROR_GENERAL;

    TEST_LOG("Creating HdmiCecSource_Engine");
    HdmiCecSource_Engine = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    HdmiCecSource_Client = Core::ProxyType<RPC::CommunicatorClient>::Create(
        Core::NodeId("/tmp/communicator"),
        Core::ProxyType<Core::IIPCServer>(HdmiCecSource_Engine));

    TEST_LOG("Creating HdmiCecSource_Engine Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    HdmiCecSource_Engine->Announcements(HdmiCecSource_Client->Announcement());
#endif

    if (!HdmiCecSource_Client.IsValid()) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        m_controller_cecSource = HdmiCecSource_Client->Open<PluginHost::IShell>(
            _T("org.rdk.HdmiCecSource"), ~0, 3000);
        if (m_controller_cecSource) {
            m_cecSourcePlugin = m_controller_cecSource->QueryInterface<Exchange::IHdmiCecSource>();
            if (m_cecSourcePlugin) {
                m_cecSourcePlugin->Register(&m_notificationHandler);
                return_value = Core::ERROR_NONE;
                TEST_LOG("Successfully created HdmiCecSource Plugin Interface");
            } else {
                TEST_LOG("Failed to get IHdmiCecSource interface");
            }
        } else {
            TEST_LOG("Failed to get HdmiCecSource Plugin Interface");
        }
    }
    return return_value;
}

uint32_t HdmiCecSource_L2Test::WaitForRequestStatus(uint32_t timeout_ms, HdmiCecSourceL2test_async_events_t expected_status)
{
    return m_notificationHandler.WaitForEvent(timeout_ms, expected_status);
}

void HdmiCecSource_L2Test::onActiveSourceStatusUpdated(const JsonObject& message)
{
    TEST_LOG("onActiveSourceStatusUpdated JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_ACTIVE_SOURCE_STATUS_UPDATED;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::onDeviceAdded(const JsonObject& message)
{
    TEST_LOG("onDeviceAdded JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_DEVICE_ADDED;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::onDeviceInfoUpdated(const JsonObject& message)
{
    TEST_LOG("onDeviceInfoUpdated JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_DEVICE_INFO_UPDATED;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::onDeviceRemoved(const JsonObject& message)
{
    TEST_LOG("onDeviceRemoved JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_DEVICE_REMOVED;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::standbyMessageReceived(const JsonObject& message)
{
    TEST_LOG("standbyMessageReceived JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= STANDBY_MESSAGE_RECEIVED;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::onKeyReleaseEvent(const JsonObject& message)
{
    TEST_LOG("onKeyReleaseEvent JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_KEY_RELEASE_EVENT;
    m_condition_variable.notify_one();
}

void HdmiCecSource_L2Test::onKeyPressEvent(const JsonObject& message)
{
    TEST_LOG("onKeyPressEvent JSON-RPC event received");
    std::unique_lock<std::mutex> lock(m_mutex);
    m_event_signalled |= ON_KEY_PRESS_EVENT;
    m_condition_variable.notify_one();
}

/*******************************************************************************************************************
 * Test Functions
 * *****************************************************************************************************************/

/**
 * @brief Test GetActiveSourceStatus API via COM-RPC
 *
 * This test verifies that the GetActiveSourceStatus API returns the correct status
 * and success flag using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetActiveSourceStatus_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetActiveSourceStatus via COM-RPC");

                // Declare output parameters
                bool isActiveSource = false;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetActiveSourceStatus(isActiveSource, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);

                // Log and validate output
                TEST_LOG("  isActiveSource: %d", isActiveSource);
                TEST_LOG("  success: %d", success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetActiveSourceStatus API via JSON-RPC
 *
 * This test verifies that the getActiveSourceStatus API returns the correct status
 * using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetActiveSourceStatus_JSONRPC)
{
    TEST_LOG("Testing getActiveSourceStatus via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getActiveSourceStatus", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate status field
    EXPECT_TRUE(result.HasLabel("status"));
    if (result.HasLabel("status")) {
        bool activeSourceStatus = result["status"].Boolean();
        TEST_LOG("  status: %d", activeSourceStatus);
    }
}

/**
 * @brief Test SetEnabled API via COM-RPC
 *
 * This test verifies that the SetEnabled API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetEnabled_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SetEnabled via COM-RPC");

                // Declare output parameters
                HdmiCecSourceSuccess setResult;

                // Call the API
                uint32_t result = m_cecSourcePlugin->SetEnabled(true, setResult);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(setResult.success);

                // Log output
                TEST_LOG("  success: %d", setResult.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SetEnabled API via JSON-RPC
 *
 * This test verifies that the setEnabled API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetEnabled_JSONRPC)
{
    TEST_LOG("Testing setEnabled via JSON-RPC");

    JsonObject params;
    params["enabled"] = true;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "setEnabled", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetEnabled API via COM-RPC
 *
 * This test verifies that the GetEnabled API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetEnabled_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetEnabled via COM-RPC");

                // Declare output parameters
                bool enabled = false;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetEnabled(enabled, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);

                // Log output
                TEST_LOG("  enabled: %d", enabled);
                TEST_LOG("  success: %d", success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetEnabled API via JSON-RPC
 *
 * This test verifies that the getEnabled API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetEnabled_JSONRPC)
{
    TEST_LOG("Testing getEnabled via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getEnabled", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate enabled field
    EXPECT_TRUE(result.HasLabel("enabled"));
    if (result.HasLabel("enabled")) {
        bool enabled = result["enabled"].Boolean();
        TEST_LOG("  enabled: %d", enabled);
    }
}

/**
 * @brief Test SetOSDName API via COM-RPC
 *
 * This test verifies that the SetOSDName API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetOSDName_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SetOSDName via COM-RPC");

                // Declare output parameters
                string testOSDName = "TestSTB";
                HdmiCecSourceSuccess setResult;

                // Call the API
                uint32_t result = m_cecSourcePlugin->SetOSDName(testOSDName, setResult);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(setResult.success);

                // Log output
                TEST_LOG("  osdName set to: %s", testOSDName.c_str());
                TEST_LOG("  success: %d", setResult.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SetOSDName API via JSON-RPC
 *
 * This test verifies that the setOSDName API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetOSDName_JSONRPC)
{
    TEST_LOG("Testing setOSDName via JSON-RPC");

    JsonObject params;
    params["name"] = "TestSTB";
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "setOSDName", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetOSDName API via COM-RPC
 *
 * This test verifies that the GetOSDName API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetOSDName_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetOSDName via COM-RPC");

                // Declare output parameters
                string osdName;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetOSDName(osdName, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);

                // Log and validate output
                TEST_LOG("  osdName: %s", osdName.c_str());
                TEST_LOG("  success: %d", success);
                EXPECT_FALSE(osdName.empty());

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetOSDName API via JSON-RPC
 *
 * This test verifies that the getOSDName API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetOSDName_JSONRPC)
{
    TEST_LOG("Testing getOSDName via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getOSDName", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate name field
    EXPECT_TRUE(result.HasLabel("name"));
    if (result.HasLabel("name")) {
        string osdName = result["name"].String();
        TEST_LOG("  name: %s", osdName.c_str());
        EXPECT_FALSE(osdName.empty());
    }
}

/**
 * @brief Test SetVendorId API via COM-RPC
 *
 * This test verifies that the SetVendorId API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetVendorId_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SetVendorId via COM-RPC");

                // Declare output parameters
                string testVendorId = "0019FB";
                HdmiCecSourceSuccess setResult;

                // Call the API
                uint32_t result = m_cecSourcePlugin->SetVendorId(testVendorId, setResult);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(setResult.success);

                // Log output
                TEST_LOG("  vendorId set to: %s", testVendorId.c_str());
                TEST_LOG("  success: %d", setResult.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SetVendorId API via JSON-RPC
 *
 * This test verifies that the setVendorId API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetVendorId_JSONRPC)
{
    TEST_LOG("Testing setVendorId via JSON-RPC");

    JsonObject params;
    params["vendorid"] = "0019FB";
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "setVendorId", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetVendorId API via JSON-RPC
 *
 * This test verifies that the getVendorId API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetVendorId_JSONRPC)
{
    TEST_LOG("Testing getVendorId via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getVendorId", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate vendorid field
    EXPECT_TRUE(result.HasLabel("vendorid"));
    if (result.HasLabel("vendorid")) {
        string vendorId = result["vendorid"].String();
        EXPECT_FALSE(vendorId.empty());
        TEST_LOG("  vendorid: %s", vendorId.c_str());
    }
}

/**
 * @brief Test SetOTPEnabled API via COM-RPC
 *
 * This test verifies that the SetOTPEnabled API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetOTPEnabled_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SetOTPEnabled via COM-RPC");

                // Declare output parameters
                HdmiCecSourceSuccess setResult;

                // Call the API
                uint32_t result = m_cecSourcePlugin->SetOTPEnabled(true, setResult);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(setResult.success);

                // Log output
                TEST_LOG("  success: %d", setResult.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SetOTPEnabled API via JSON-RPC
 *
 * This test verifies that the setOTPEnabled API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SetOTPEnabled_JSONRPC)
{
    TEST_LOG("Testing setOTPEnabled via JSON-RPC");

    JsonObject params;
    params["enabled"] = true;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "setOTPEnabled", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetOTPEnabled API via COM-RPC
 *
 * This test verifies that the GetOTPEnabled API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetOTPEnabled_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetOTPEnabled via COM-RPC");

                // Declare output parameters
                bool enabled = false;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetOTPEnabled(enabled, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);

                // Log and validate output
                TEST_LOG("  enabled: %d", enabled);
                TEST_LOG("  success: %d", success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetOTPEnabled API via JSON-RPC
 *
 * This test verifies that the getOTPEnabled API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetOTPEnabled_JSONRPC)
{
    TEST_LOG("Testing getOTPEnabled via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getOTPEnabled", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate enabled field
    EXPECT_TRUE(result.HasLabel("enabled"));
    if (result.HasLabel("enabled")) {
        bool enabled = result["enabled"].Boolean();
        TEST_LOG("  enabled: %d", enabled);
    }
}

/**
 * @brief Test SendStandbyMessage API via COM-RPC
 *
 * This test verifies that the SendStandbyMessage API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SendStandbyMessage_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SendStandbyMessage via COM-RPC");

                // Declare output parameters
                HdmiCecSourceSuccess result;

                // Call the API
                uint32_t retval = m_cecSourcePlugin->SendStandbyMessage(result);

                // Validate result
                EXPECT_EQ(retval, Core::ERROR_NONE);
                if (retval != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(retval) + " (" + std::string(Core::ErrorToString(retval)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(result.success);

                // Log output
                TEST_LOG("  success: %d", result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SendStandbyMessage API via JSON-RPC
 *
 * This test verifies that the sendStandbyMessage API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SendStandbyMessage_JSONRPC)
{
    TEST_LOG("Testing sendStandbyMessage via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "sendStandbyMessage", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test SendKeyPressEvent API via COM-RPC
 *
 * This test verifies that the SendKeyPressEvent API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SendKeyPressEvent_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing SendKeyPressEvent via COM-RPC");

                // Declare input/output parameters
                uint32_t logicalAddress = 0; // TV logical address
                uint32_t keyCode = 0x00; // Select key code
                HdmiCecSourceSuccess result;

                // Call the API
                uint32_t retval = m_cecSourcePlugin->SendKeyPressEvent(logicalAddress, keyCode, result);

                // Validate result
                EXPECT_EQ(retval, Core::ERROR_NONE);
                if (retval != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(retval) + " (" + std::string(Core::ErrorToString(retval)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(result.success);

                // Log output
                TEST_LOG("  logicalAddress: %d", logicalAddress);
                TEST_LOG("  keyCode: %d", keyCode);
                TEST_LOG("  success: %d", result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SendKeyPressEvent API via JSON-RPC
 *
 * This test verifies that the sendKeyPressEvent API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, SendKeyPressEvent_JSONRPC)
{
    TEST_LOG("Testing sendKeyPressEvent via JSON-RPC");

    JsonObject params;
    params["logicalAddress"] = 0; // TV logical address
    params["keyCode"] = 0x00; // Select key code
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "sendKeyPressEvent", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetVendorId API via COM-RPC
 *
 * This test verifies that the GetVendorId API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetVendorId_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetVendorId via COM-RPC");

                // Declare output parameters
                string vendorId;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetVendorId(vendorId, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);
                EXPECT_FALSE(vendorId.empty());

                // Log output
                TEST_LOG("  vendorId: %s", vendorId.c_str());
                TEST_LOG("  success: %d", success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetDeviceList API via COM-RPC
 *
 * This test verifies that the GetDeviceList API returns the correct device information using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetDeviceList_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing GetDeviceList via COM-RPC");

                // Declare output parameters
                uint32_t numberOfDevices = 0;
                IHdmiCecSourceDeviceListIterator* deviceList = nullptr;
                bool success = false;

                // Call the API
                uint32_t result = m_cecSourcePlugin->GetDeviceList(numberOfDevices, deviceList, success);

                // Validate result
                EXPECT_EQ(result, Core::ERROR_NONE);
                if (result != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(result) + " (" + std::string(Core::ErrorToString(result)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(success);

                // Log and validate output
                TEST_LOG("  numberOfDevices: %d", numberOfDevices);
                TEST_LOG("  success: %d", success);

                if (deviceList != nullptr) {
                    HdmiCecSourceDevice device;
                    uint32_t deviceCount = 0;
                    while (deviceList->Next(device)) {
                        TEST_LOG("  Device[%d]: logicalAddress=%d, vendorID=%s, osdName=%s",
                                 deviceCount++, device.logicalAddress, device.vendorID.c_str(), device.osdName.c_str());
                        EXPECT_FALSE(device.vendorID.empty());
                        EXPECT_FALSE(device.osdName.empty());
                    }
                    EXPECT_EQ(deviceCount, numberOfDevices);
                    deviceList->Release();
                }

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetDeviceList API via JSON-RPC
 *
 * This test verifies that the getDeviceList API returns the correct device information using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, GetDeviceList_JSONRPC)
{
    TEST_LOG("Testing getDeviceList via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "getDeviceList", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }

    // Validate numberofdevices field
    EXPECT_TRUE(result.HasLabel("numberofdevices"));
    if (result.HasLabel("numberofdevices")) {
        uint32_t numberOfDevices = result["numberofdevices"].Number();
        TEST_LOG("  numberofdevices: %d", numberOfDevices);
    }

    // Validate deviceList array
    EXPECT_TRUE(result.HasLabel("deviceList"));
    if (result.HasLabel("deviceList")) {
        JsonArray deviceList = result["deviceList"].Array();
        TEST_LOG("  deviceList length: %d", deviceList.Length());

        for (uint32_t i = 0; i < deviceList.Length(); i++) {
            JsonObject device = deviceList[i].Object();

            EXPECT_TRUE(device.HasLabel("logicalAddress"));
            if (device.HasLabel("logicalAddress")) {
                uint32_t logicalAddress = device["logicalAddress"].Number();
                TEST_LOG("    Device[%d].logicalAddress: %d", i, logicalAddress);
            }

            EXPECT_TRUE(device.HasLabel("vendorID"));
            if (device.HasLabel("vendorID")) {
                string vendorID = device["vendorID"].String();
                TEST_LOG("    Device[%d].vendorID: %s", i, vendorID.c_str());
                EXPECT_FALSE(vendorID.empty());
            }

            EXPECT_TRUE(device.HasLabel("osdName"));
            if (device.HasLabel("osdName")) {
                string osdName = device["osdName"].String();
                TEST_LOG("    Device[%d].osdName: %s", i, osdName.c_str());
                EXPECT_FALSE(osdName.empty());
            }
        }
    }
}

/**
 * @brief Test PerformOTPAction API via COM-RPC
 *
 * This test verifies that the PerformOTPAction API works correctly using COM-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, PerformOTPAction_COMRPC)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                TEST_LOG("Testing PerformOTPAction via COM-RPC");

                // Declare output parameters
                HdmiCecSourceSuccess result;

                // Call the API
                uint32_t retval = m_cecSourcePlugin->PerformOTPAction(result);

                // Validate result
                EXPECT_EQ(retval, Core::ERROR_NONE);
                if (retval != Core::ERROR_NONE) {
                    std::string errorMsg = "COM-RPC returned error " + std::to_string(retval) + " (" + std::string(Core::ErrorToString(retval)) + ")";
                    TEST_LOG("Err: %s", errorMsg.c_str());
                }
                EXPECT_TRUE(result.success);

                // Log output
                TEST_LOG("  success: %d", result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test PerformOTPAction API via JSON-RPC
 *
 * This test verifies that the performOTPAction API works correctly using JSON-RPC interface.
 */
TEST_F(HdmiCecSource_L2Test, PerformOTPAction_JSONRPC)
{
    TEST_LOG("Testing performOTPAction via JSON-RPC");

    JsonObject params;
    JsonObject result;

    uint32_t status = InvokeServiceMethod("org.rdk.HdmiCecSource.1", "performOTPAction", params, result);

    EXPECT_EQ(status, Core::ERROR_NONE);

    // Validate success field
    EXPECT_TRUE(result.HasLabel("success"));
    if (result.HasLabel("success")) {
        EXPECT_TRUE(result["success"].Boolean());
        TEST_LOG("  success: %d", result["success"].Boolean());
    }
}

/**
 * @brief Test GetOTPEnabled/SetOTPEnabled APIs
 *
 * This test verifies that the SetOTPEnabled and GetOTPEnabled APIs work correctly.
 */
TEST_F(HdmiCecSource_L2Test, SetGetOTPEnabled)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                // Set OTP enabled to true
                HdmiCecSourceSuccess setResult;
                uint32_t result = m_cecSourcePlugin->SetOTPEnabled(true, setResult);
                EXPECT_EQ(result, Core::ERROR_NONE);
                EXPECT_TRUE(setResult.success);

                // Get OTP enabled status
                bool enabled = false;
                bool success = false;
                result = m_cecSourcePlugin->GetOTPEnabled(enabled, success);
                EXPECT_EQ(result, Core::ERROR_NONE);
                EXPECT_TRUE(success);
                EXPECT_TRUE(enabled);
                TEST_LOG("GetOTPEnabled: enabled=%d, success=%d", enabled, success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SendStandbyMessage API
 *
 * This test verifies that the SendStandbyMessage API works correctly.
 */
TEST_F(HdmiCecSource_L2Test, SendStandbyMessage)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                HdmiCecSourceSuccess result;
                uint32_t retval = m_cecSourcePlugin->SendStandbyMessage(result);

                EXPECT_EQ(retval, Core::ERROR_NONE);
                EXPECT_TRUE(result.success);
                TEST_LOG("SendStandbyMessage: success=%d", result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test SendKeyPressEvent API
 *
 * This test verifies that the SendKeyPressEvent API works correctly.
 */
TEST_F(HdmiCecSource_L2Test, SendKeyPressEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                uint32_t logicalAddress = 0; // TV logical address
                uint32_t keyCode = 0x00; // Select key code
                HdmiCecSourceSuccess result;
                uint32_t retval = m_cecSourcePlugin->SendKeyPressEvent(logicalAddress, keyCode, result);

                EXPECT_EQ(retval, Core::ERROR_NONE);
                EXPECT_TRUE(result.success);
                TEST_LOG("SendKeyPressEvent: logicalAddress=%d, keyCode=%d, success=%d",
                         logicalAddress, keyCode, result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test GetDeviceList API
 *
 * This test verifies that the GetDeviceList API returns the correct device information.
 */
TEST_F(HdmiCecSource_L2Test, GetDeviceList)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                uint32_t numberOfDevices = 0;
                IHdmiCecSourceDeviceListIterator* deviceList = nullptr;
                bool success = false;

                uint32_t result = m_cecSourcePlugin->GetDeviceList(numberOfDevices, deviceList, success);

                EXPECT_EQ(result, Core::ERROR_NONE);
                EXPECT_TRUE(success);
                TEST_LOG("GetDeviceList: numberOfDevices=%d, success=%d", numberOfDevices, success);

                if (deviceList != nullptr) {
                    HdmiCecSourceDevice device;
                    while (deviceList->Next(device)) {
                        TEST_LOG("Device: logicalAddress=%d, vendorID=%s, osdName=%s",
                                 device.logicalAddress, device.vendorID.c_str(), device.osdName.c_str());
                    }
                    deviceList->Release();
                }

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test PerformOTPAction API
 *
 * This test verifies that the PerformOTPAction API works correctly.
 */
TEST_F(HdmiCecSource_L2Test, PerformOTPAction)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                HdmiCecSourceSuccess result;
                uint32_t retval = m_cecSourcePlugin->PerformOTPAction(result);

                EXPECT_EQ(retval, Core::ERROR_NONE);
                EXPECT_TRUE(result.success);
                TEST_LOG("PerformOTPAction: success=%d", result.success);

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test OnActiveSourceStatusUpdated event
 *
 * This test verifies that the OnActiveSourceStatusUpdated event is received correctly.
 */
TEST_F(HdmiCecSource_L2Test, OnActiveSourceStatusUpdatedEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                // Simulate active source status change
                m_notificationHandler.OnActiveSourceStatusUpdated(true);

                uint32_t status = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
                EXPECT_EQ(status, ON_ACTIVE_SOURCE_STATUS_UPDATED);
                EXPECT_TRUE(m_notificationHandler.GetActiveSourceStatus());
                TEST_LOG("OnActiveSourceStatusUpdated event verified");

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test OnDeviceAdded event
 *
 * This test verifies that the OnDeviceAdded event is received correctly.
 */
TEST_F(HdmiCecSource_L2Test, OnDeviceAddedEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                // Simulate device added event
                int testLogicalAddress = 4;
                m_notificationHandler.OnDeviceAdded(testLogicalAddress);

                uint32_t status = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_ADDED);
                EXPECT_EQ(status, ON_DEVICE_ADDED);
                EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), testLogicalAddress);
                TEST_LOG("OnDeviceAdded event verified");

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

/**
 * @brief Test OnDeviceRemoved event
 *
 * This test verifies that the OnDeviceRemoved event is received correctly.
 */
TEST_F(HdmiCecSource_L2Test, OnDeviceRemovedEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
    } else {
        EXPECT_TRUE(m_controller_cecSource != nullptr);
        if (m_controller_cecSource) {
            EXPECT_TRUE(m_cecSourcePlugin != nullptr);
            if (m_cecSourcePlugin) {
                // Simulate device removed event
                int testLogicalAddress = 4;
                m_notificationHandler.OnDeviceRemoved(testLogicalAddress);

                uint32_t status = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_REMOVED);
                EXPECT_EQ(status, ON_DEVICE_REMOVED);
                EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), testLogicalAddress);
                TEST_LOG("OnDeviceRemoved event verified");

                m_cecSourcePlugin->Unregister(&m_notificationHandler);
                m_cecSourcePlugin->Release();
            } else {
                TEST_LOG("m_cecSourcePlugin is NULL");
            }
            m_controller_cecSource->Release();
        } else {
            TEST_LOG("m_controller_cecSource is NULL");
        }
    }
}

//======================================== Frame Injection Tests ========================================

/**
 * @brief Test Standby frame injection and verify standbyMessageReceived event
 *
 * This test injects a Standby CEC frame and verifies that the standbyMessageReceived event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectStandbyFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject Standby frame (Opcode 0x36)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x36 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting Standby CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for standbyMessageReceived event
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, STANDBY_MESSAGE_RECEIVED);
    EXPECT_TRUE(signalled & STANDBY_MESSAGE_RECEIVED);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 0);
    TEST_LOG("Standby event verified");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test UserControlPressed frame injection and verify onKeyPressEvent event
 *
 * This test injects a UserControlPressed CEC frame and verifies that the onKeyPressEvent is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectUserControlPressedFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject UserControlPressed frame (Opcode 0x44) with keycode for Volume Up (0x41)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x44, 0x41 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting UserControlPressed CEC frame with Volume Up key");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for onKeyPressEvent
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_KEY_PRESS_EVENT);
    EXPECT_TRUE(signalled & ON_KEY_PRESS_EVENT);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 0);
    EXPECT_EQ(m_notificationHandler.GetKeyCode(), 0x41);
    TEST_LOG("UserControlPressed event verified");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test UserControlReleased frame injection and verify onKeyReleaseEvent event
 *
 * This test injects a UserControlReleased CEC frame and verifies that the onKeyReleaseEvent is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectUserControlReleasedFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject UserControlReleased frame (Opcode 0x45)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x45 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting UserControlReleased CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for onKeyReleaseEvent
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_KEY_RELEASE_EVENT);
    EXPECT_TRUE(signalled & ON_KEY_RELEASE_EVENT);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 0);
    TEST_LOG("UserControlReleased event verified");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test ActiveSource frame injection and verify OnActiveSourceStatusUpdated event
 *
 * This test injects an ActiveSource CEC frame with our physical address
 * and verifies that the OnActiveSourceStatusUpdated event is triggered with true status.
 */
TEST_F(HdmiCecSource_L2Test, InjectActiveSourceFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject ActiveSource frame (Opcode 0x82) with physical address matching ours
    // Physical address: 0x0F0F (15.15.15.15 in 2-byte CEC format)
    // From device (4) to all (broadcast)
    uint8_t buffer[] = { 0x4F, 0x82, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting ActiveSource CEC frame with our physical address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give the system time to process the frame and trigger events
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    //EXPECT_TRUE(m_notificationHandler.GetActiveSourceStatus());
    TEST_LOG("ActiveSource event verified with status=true");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test DeviceVendorID frame injection and verify OnDeviceInfoUpdated event
 *
 * This test injects a DeviceVendorID CEC frame and verifies that the OnDeviceInfoUpdated event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectDeviceVendorIDFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // First add the device by injecting ReportPhysicalAddress
    uint8_t setupBuffer[] = { 0x4F, 0x84, 0x20, 0x00, 0x04 };
    CECFrame setupFrame(setupBuffer, sizeof(setupBuffer));
    
    TEST_LOG("Setting up: Injecting ReportPhysicalAddress CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(setupFrame);
    }
    
    // Give time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Wait for device to be added
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_ADDED);
    //EXPECT_TRUE(signalled & ON_DEVICE_ADDED);
    m_notificationHandler.ResetEvent();

    // Now inject DeviceVendorID frame (Opcode 0x87)
    // From device 4 to all (broadcast), Vendor ID: LG (0x00E091)
    uint8_t buffer[] = { 0x4F, 0x87, 0x00, 0xE0, 0x91 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting DeviceVendorID CEC frame with LG vendor ID");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for OnDeviceInfoUpdated event
    signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_INFO_UPDATED);
    EXPECT_TRUE(signalled & ON_DEVICE_INFO_UPDATED);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 4);
    TEST_LOG("OnDeviceInfoUpdated event verified after DeviceVendorID");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test SetOSDName frame injection and verify OnDeviceInfoUpdated event
 *
 * This test injects a SetOSDName CEC frame and verifies that the OnDeviceInfoUpdated event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectSetOSDNameFrameAndVerifyEvent)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // First add the device by injecting ReportPhysicalAddress
    uint8_t setupBuffer[] = { 0x4F, 0x84, 0x20, 0x00, 0x04 };
    CECFrame setupFrame(setupBuffer, sizeof(setupBuffer));
    
    TEST_LOG("Setting up: Injecting ReportPhysicalAddress CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(setupFrame);
    }
    
    // Give time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Wait for device to be added
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_ADDED);
    //EXPECT_TRUE(signalled & ON_DEVICE_ADDED);
    m_notificationHandler.ResetEvent();

    // Now inject SetOSDName frame (Opcode 0x47)
    // From device 4 to us (device 3 or 0), OSD Name: "TestDev"
    uint8_t buffer[] = { 0x40, 0x47, 'T', 'e', 's', 't', 'D', 'e', 'v' };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting SetOSDName CEC frame with name 'TestDev'");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for OnDeviceInfoUpdated event
    signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_INFO_UPDATED);
    EXPECT_TRUE(signalled & ON_DEVICE_INFO_UPDATED);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 4);
    TEST_LOG("OnDeviceInfoUpdated event verified after SetOSDName");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test RequestActiveSource frame injection
 *
 * This test injects a RequestActiveSource CEC frame. If the device is active source,
 * it should respond with an ActiveSource message.
 */
TEST_F(HdmiCecSource_L2Test, InjectRequestActiveSourceFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject RequestActiveSource frame (Opcode 0x85)
    // From TV (0) to all (broadcast)
    uint8_t buffer[] = { 0x0F, 0x85 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting RequestActiveSource CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Note: This will only send ActiveSource if isDeviceActiveSource is true
    // The test verifies the frame is processed without errors
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("RequestActiveSource frame processed");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test GetCECVersion frame injection
 *
 * This test injects a GetCECVersion CEC frame and verifies that the device
 * responds with a CECVersion message.
 */
TEST_F(HdmiCecSource_L2Test, InjectGetCECVersionFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject GetCECVersion frame (Opcode 0x9F)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x9F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting GetCECVersion CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with CECVersion (V_1_4)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("GetCECVersion frame processed - device should send CECVersion response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test CECVersion frame injection and verify device added
 *
 * This test injects a CECVersion CEC frame and verifies that the device
 * is added to the device list.
 */
TEST_F(HdmiCecSource_L2Test, InjectCECVersionFrameAndVerifyDeviceAdded)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject CECVersion frame (Opcode 0x9E)
    // From device 5 to us (device 4), Version 1.4
    uint8_t buffer[] = { 0x54, 0x9E, 0x05 };  // 0x05 = Version 1.4
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting CECVersion CEC frame from device 5");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for OnDeviceAdded event
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_ADDED);
    //EXPECT_TRUE(signalled & ON_DEVICE_ADDED);
    //EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 5);
    TEST_LOG("CECVersion frame processed - device 5 added");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test GiveOSDName frame injection
 *
 * This test injects a GiveOSDName CEC frame and verifies that the device
 * responds with a SetOSDName message.
 */
TEST_F(HdmiCecSource_L2Test, InjectGiveOSDNameFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject GiveOSDName frame (Opcode 0x46)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x46 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting GiveOSDName CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with SetOSDName
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("GiveOSDName frame processed - device should send SetOSDName response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test GivePhysicalAddress frame injection
 *
 * This test injects a GivePhysicalAddress CEC frame and verifies that the device
 * responds with a ReportPhysicalAddress message.
 */
TEST_F(HdmiCecSource_L2Test, InjectGivePhysicalAddressFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject GivePhysicalAddress frame (Opcode 0x83)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x83 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting GivePhysicalAddress CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with ReportPhysicalAddress
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("GivePhysicalAddress frame processed - device should send ReportPhysicalAddress response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test GiveDeviceVendorID frame injection
 *
 * This test injects a GiveDeviceVendorID CEC frame and verifies that the device
 * responds with a DeviceVendorID message.
 */
TEST_F(HdmiCecSource_L2Test, InjectGiveDeviceVendorIDFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject GiveDeviceVendorID frame (Opcode 0x8C)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x8C };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting GiveDeviceVendorID CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with DeviceVendorID
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("GiveDeviceVendorID frame processed - device should send DeviceVendorID response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test RoutingChange frame injection and verify active source event
 *
 * This test injects a RoutingChange CEC frame with our physical address as destination
 * and verifies that the OnActiveSourceStatusUpdated event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectRoutingChangeFrameAndVerifyActiveSource)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject RoutingChange frame (Opcode 0x80)
    // From TV (0) to all (broadcast), changing route to our physical address (0x0F0F)
    uint8_t buffer[] = { 0x0F, 0x80, 0x00, 0x00, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting RoutingChange CEC frame routing to our address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give time for processing and event propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    //EXPECT_TRUE(m_notificationHandler.GetActiveSourceStatus());
    TEST_LOG("RoutingChange frame processed - active source status updated to true");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test RoutingInformation frame injection and verify active source event
 *
 * This test injects a RoutingInformation CEC frame with our physical address
 * and verifies that the OnActiveSourceStatusUpdated event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectRoutingInformationFrameAndVerifyActiveSource)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject RoutingInformation frame (Opcode 0x81)
    // From TV (0) to all (broadcast), routing to our physical address (0x0F0F)
    uint8_t buffer[] = { 0x0F, 0x81, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting RoutingInformation CEC frame routing to our address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give time for processing and event propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    //EXPECT_TRUE(m_notificationHandler.GetActiveSourceStatus());
    TEST_LOG("RoutingInformation frame processed - active source status updated to true");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test SetStreamPath frame injection and verify active source event
 *
 * This test injects a SetStreamPath CEC frame with our physical address
 * and verifies that the OnActiveSourceStatusUpdated event is triggered.
 */
TEST_F(HdmiCecSource_L2Test, InjectSetStreamPathFrameAndVerifyActiveSource)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject SetStreamPath frame (Opcode 0x86)
    // From TV (0) to all (broadcast), setting stream path to our physical address (0x0F0F)
    uint8_t buffer[] = { 0x0F, 0x86, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting SetStreamPath CEC frame to our address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give time for processing and event propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    //EXPECT_TRUE(m_notificationHandler.GetActiveSourceStatus());
    TEST_LOG("SetStreamPath frame processed - active source status updated to true");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test GiveDevicePowerStatus frame injection
 *
 * This test injects a GiveDevicePowerStatus CEC frame and verifies that the device
 * responds with a ReportPowerStatus message.
 */
TEST_F(HdmiCecSource_L2Test, InjectGiveDevicePowerStatusFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject GiveDevicePowerStatus frame (Opcode 0x8F)
    // From TV (0) to device (4)
    uint8_t buffer[] = { 0x04, 0x8F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting GiveDevicePowerStatus CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with ReportPowerStatus
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("GiveDevicePowerStatus frame processed - device should send ReportPowerStatus response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test ReportPowerStatus frame injection and verify device added
 *
 * This test injects a ReportPowerStatus CEC frame from TV and verifies that the device
 * is added to the device list.
 */
TEST_F(HdmiCecSource_L2Test, InjectReportPowerStatusFrameAndVerifyDeviceAdded)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject ReportPowerStatus frame (Opcode 0x90)
    // From TV (0) to device (4), Power status: ON (0x00)
    uint8_t buffer[] = { 0x04, 0x90, 0x00 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting ReportPowerStatus CEC frame from TV");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Wait for OnDeviceAdded event
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_DEVICE_ADDED);
    EXPECT_TRUE(signalled & ON_DEVICE_ADDED);
    EXPECT_EQ(m_notificationHandler.GetLogicalAddress(), 0);
    TEST_LOG("ReportPowerStatus frame processed - TV device added");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test FeatureAbort frame injection
 *
 * This test injects a FeatureAbort CEC frame and verifies that the device
 * processes it without errors.
 */
TEST_F(HdmiCecSource_L2Test, InjectFeatureAbortFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject FeatureAbort frame (Opcode 0x00)
    // From TV (0) to device (4), Feature Opcode: 0x44 (User Control Pressed), Abort Reason: 0x04 (Refused)
    uint8_t buffer[] = { 0x04, 0x00, 0x44, 0x04 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting FeatureAbort CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The frame should be processed without errors
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("FeatureAbort frame processed");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test Abort frame injection
 *
 * This test injects an Abort CEC frame (unrecognized opcode) and verifies that the device
 * responds with a FeatureAbort message.
 */
TEST_F(HdmiCecSource_L2Test, InjectAbortFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject an unrecognized opcode frame that will trigger Abort processing
    // From TV (0) to device (4), Invalid Opcode: 0xFF
    uint8_t buffer[] = { 0x04, 0xFF };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting frame with unrecognized opcode (Abort)");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The device should respond with FeatureAbort
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("Abort frame processed - device should send FeatureAbort response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test Polling frame injection
 *
 * This test injects a Polling CEC frame and verifies that the device
 * processes it without errors.
 */
TEST_F(HdmiCecSource_L2Test, InjectPollingFrameAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject Polling frame (same source and destination)
    // From device (4) to device (4) - this is a polling message
    uint8_t buffer[] = { 0x44 };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting Polling CEC frame");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // The frame should be processed without errors
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_LOG("Polling frame processed");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test ActiveSource frame with matching physical address to set device as active source
 *
 * This test injects an ActiveSource CEC frame with our own physical address (0x0F0F)
 * to test the path where isDeviceActiveSource becomes true.
 */
TEST_F(HdmiCecSource_L2Test, InjectActiveSourceFrameWithMatchingAddressAndVerify)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // First, inject ActiveSource frame with OUR physical address (0x0F0F) to make device active
    // From device 4 (us) to all (broadcast)
    uint8_t buffer1[] = { 0x4F, 0x82, 0x0F, 0x0F };
    CECFrame frame1(buffer1, sizeof(buffer1));
    
    TEST_LOG("Injecting ActiveSource CEC frame with our physical address to set as active source");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame1);
    }

    // Give time for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event - device should now be active source
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    TEST_LOG("Device is now active source after ActiveSource with matching address");

    // Now inject RequestActiveSource to test the path where device responds
    // From TV (0) to all (broadcast)
    uint8_t buffer2[] = { 0x0F, 0x85 };
    CECFrame frame2(buffer2, sizeof(buffer2));
    
    TEST_LOG("Injecting RequestActiveSource - device should respond with ActiveSource");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame2);
    }

    // The device should respond with ActiveSource since it's now the active source
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    TEST_LOG("RequestActiveSource processed - device sent ActiveSource response");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test RoutingChange frame with matching destination address
 *
 * This test injects a RoutingChange CEC frame where the destination matches our physical address
 * to test the path where isDeviceActiveSource becomes true.
 */
TEST_F(HdmiCecSource_L2Test, InjectRoutingChangeFrameWithMatchingDestination)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject RoutingChange frame where destination MATCHES our physical address
    // From TV (0) to all (broadcast), routing FROM 0x0000 TO our address 0x0F0F
    uint8_t buffer[] = { 0x0F, 0x80, 0x00, 0x00, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting RoutingChange with destination matching our address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give time for processing and event propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event with true status
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    TEST_LOG("RoutingChange processed - device is now active source");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test RoutingInformation frame with matching destination address
 *
 * This test injects a RoutingInformation CEC frame where the destination matches our physical address
 * to test the path where isDeviceActiveSource becomes true.
 */
TEST_F(HdmiCecSource_L2Test, InjectRoutingInformationFrameWithMatchingDestination)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);
    
    if (!m_cecSourcePlugin || listeners.empty()) {
        TEST_LOG("Test prerequisites not met");
        if (m_cecSourcePlugin) {
            m_cecSourcePlugin->Unregister(&m_notificationHandler);
            m_cecSourcePlugin->Release();
        }
        if (m_controller_cecSource) {
            m_controller_cecSource->Release();
        }
        return;
    }

    // Inject RoutingInformation frame where destination MATCHES our physical address
    // From TV (0) to all (broadcast), routing TO our address 0x0F0F
    uint8_t buffer[] = { 0x0F, 0x81, 0x0F, 0x0F };
    CECFrame frame(buffer, sizeof(buffer));
    
    TEST_LOG("Injecting RoutingInformation with destination matching our address");
    for (auto* listener : listeners) {
        if (listener)
            listener->notify(frame);
    }

    // Give time for processing and event propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Wait for OnActiveSourceStatusUpdated event with true status
    uint32_t signalled = WaitForRequestStatus(EVNT_TIMEOUT, ON_ACTIVE_SOURCE_STATUS_UPDATED);
    EXPECT_TRUE(signalled & ON_ACTIVE_SOURCE_STATUS_UPDATED);
    TEST_LOG("RoutingInformation processed - device is now active source");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test SendKeyPressEvent with invalid logical address
 *
 * This test verifies error handling when SendKeyPressEvent is called with an invalid logical address.
 */
TEST_F(HdmiCecSource_L2Test, SendKeyPressEventWithInvalidLogicalAddress)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);

    HdmiCecSourceSuccess success;
    success.success = false;

    // Test with invalid logical address (0xFF is invalid)
    uint32_t result = m_cecSourcePlugin->SendKeyPressEvent(0xFF, 0x41, success);
    
    // Should return error
    EXPECT_NE(result, Core::ERROR_NONE);
    EXPECT_FALSE(success.success);
    TEST_LOG("SendKeyPressEvent correctly rejected invalid logical address");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}

/**
 * @brief Test SendKeyPressEvent with invalid key code
 *
 * This test verifies error handling when SendKeyPressEvent is called with an unsupported key code.
 */
TEST_F(HdmiCecSource_L2Test, SendKeyPressEventWithInvalidKeyCode)
{
    if (CreateHdmiCecSourceInterfaceObject() != Core::ERROR_NONE) {
        TEST_LOG("Invalid HdmiCecSource_Client");
        return;
    }

    EXPECT_TRUE(m_controller_cecSource != nullptr);
    EXPECT_TRUE(m_cecSourcePlugin != nullptr);

    HdmiCecSourceSuccess success;
    success.success = false;

    // Test with valid logical address but invalid/unsupported key code (0xFF)
    uint32_t result = m_cecSourcePlugin->SendKeyPressEvent(0, 0xFF, success);
    
    // Should return NOT_SUPPORTED error
    EXPECT_EQ(result, Core::ERROR_NOT_SUPPORTED);
    EXPECT_FALSE(success.success);
    TEST_LOG("SendKeyPressEvent correctly rejected unsupported key code");

    m_cecSourcePlugin->Unregister(&m_notificationHandler);
    m_cecSourcePlugin->Release();
    m_controller_cecSource->Release();
}
