#include "core/psdk_lifecycle.h"

#include <dji_aircraft_info.h>
#include <dji_core.h>
#include <dji_logger.h>
#include <dji_platform.h>

#include <algorithm>
#include <cstdio>

#include "core/manifold3_app_info.h"
#include "core/psdk_user_info.h"
#include "platform/hal_usb_bulk.h"
#include "platform/osal.h"
#include "platform/osal_fs.h"
#include "platform/osal_socket.h"

namespace manifold3 {
namespace {

constexpr char kAppAlias[] = "MANIFOLD3_VISION_DETECT";
constexpr char kAppSerialNumber[] = "M3VD000000000001";
constexpr T_DjiFirmwareVersion kFirmwareVersion = {
    .majorVersion = 1, .minorVersion = 0, .modifyVersion = 0, .debugVersion = 0};

T_DjiReturnCode PrintConsole(const uint8_t *data, uint16_t dataLen) {
    (void)dataLen;
    std::fwrite(data, 1, dataLen, stdout);
    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

} // namespace

PsdkLifecycle &PsdkLifecycle::Get() {
    static PsdkLifecycle instance;
    return instance;
}

bool PsdkLifecycle::Initialize() {
    if (initialized_) {
        return true;
    }

    T_DjiOsalHandler osalHandler = {
        .TaskCreate = Osal_TaskCreate,
        .TaskDestroy = Osal_TaskDestroy,
        .TaskSleepMs = Osal_TaskSleepMs,
        .MutexCreate = Osal_MutexCreate,
        .MutexDestroy = Osal_MutexDestroy,
        .MutexLock = Osal_MutexLock,
        .MutexUnlock = Osal_MutexUnlock,
        .SemaphoreCreate = Osal_SemaphoreCreate,
        .SemaphoreDestroy = Osal_SemaphoreDestroy,
        .SemaphoreWait = Osal_SemaphoreWait,
        .SemaphoreTimedWait = Osal_SemaphoreTimedWait,
        .SemaphorePost = Osal_SemaphorePost,
        .GetTimeMs = Osal_GetTimeMs,
        .GetTimeUs = Osal_GetTimeUs,
        .GetRandomNum = Osal_GetRandomNum,
        .Malloc = Osal_Malloc,
        .Free = Osal_Free,
    };

    T_DjiLoggerConsole printConsole = {
        .func = PrintConsole,
        .consoleLevel = DJI_LOGGER_CONSOLE_LOG_LEVEL_INFO,
        .isSupportColor = true,
    };

    T_DjiHalUsbBulkHandler usbBulkHandler = {
        .UsbBulkInit = HalUsbBulk_Init,
        .UsbBulkDeInit = HalUsbBulk_DeInit,
        .UsbBulkWriteData = HalUsbBulk_WriteData,
        .UsbBulkReadData = HalUsbBulk_ReadData,
        .UsbBulkGetDeviceInfo = HalUsbBulk_GetDeviceInfo,
    };

    T_DjiFileSystemHandler fileSystemHandler = {
        .FileOpen = Osal_FileOpen,
        .FileClose = Osal_FileClose,
        .FileWrite = Osal_FileWrite,
        .FileRead = Osal_FileRead,
        .FileSeek = Osal_FileSeek,
        .FileSync = Osal_FileSync,
        .DirOpen = Osal_DirOpen,
        .DirClose = Osal_DirClose,
        .DirRead = Osal_DirRead,
        .Mkdir = Osal_Mkdir,
        .Unlink = Osal_Unlink,
        .Rename = Osal_Rename,
        .Stat = Osal_Stat,
    };

    T_DjiSocketHandler socketHandler = {
        .Socket = Osal_Socket,
        .Close = Osal_Close,
        .Bind = Osal_Bind,
        .UdpSendData = Osal_UdpSendData,
        .UdpRecvData = Osal_UdpRecvData,
        .TcpListen = Osal_TcpListen,
        .TcpAccept = Osal_TcpAccept,
        .TcpConnect = Osal_TcpConnect,
        .TcpSendData = Osal_TcpSendData,
        .TcpRecvData = Osal_TcpRecvData,
    };

    if (DjiPlatform_RegOsalHandler(&osalHandler) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "register OSAL handler failed\n");
        return false;
    }
    if (DjiLogger_AddConsole(&printConsole) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "register logger console failed\n");
        return false;
    }
    if (DjiPlatform_RegHalUsbBulkHandler(&usbBulkHandler) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "register USB bulk handler failed\n");
        return false;
    }
    if (DjiPlatform_RegSocketHandler(&socketHandler) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "register socket handler failed\n");
        return false;
    }
    if (DjiPlatform_RegFileSystemHandler(&fileSystemHandler) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "register filesystem handler failed\n");
        return false;
    }

    T_DjiUserInfo userInfo;
    PsdkCredentialStrings credentials = {MANIFOLD3_APP_NAME,          MANIFOLD3_APP_ID,
                                         MANIFOLD3_APP_KEY,           MANIFOLD3_APP_LICENSE,
                                         MANIFOLD3_DEVELOPER_ACCOUNT, MANIFOLD3_BAUD_RATE};
    if (!FillPsdkUserInfo(credentials, &userInfo)) {
        return false;
    }

    if (DjiCore_Init(&userInfo) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "DjiCore_Init failed\n");
        return false;
    }

    T_DjiAircraftInfoBaseInfo aircraftInfo;
    if (DjiAircraftInfo_GetBaseInfo(&aircraftInfo) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "DjiAircraftInfo_GetBaseInfo failed\n");
        Shutdown();
        return false;
    }

    T_DjiAircraftVersion aircraftVersion;
    if (DjiAircraftInfo_GetAircraftVersion(&aircraftVersion) == DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::printf("Aircraft version: V%02u.%02u.%02u.%02u\n", aircraftVersion.majorVersion,
                    aircraftVersion.minorVersion, aircraftVersion.modifyVersion, aircraftVersion.debugVersion);
    }

    if (DjiCore_SetAlias(kAppAlias) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ||
        DjiCore_SetFirmwareVersion(kFirmwareVersion) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS ||
        DjiCore_SetSerialNumber(kAppSerialNumber) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "set product identity failed\n");
        Shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

bool PsdkLifecycle::Start() {
    if (!initialized_) {
        std::fprintf(stderr, "PsdkLifecycle::Start called before Initialize\n");
        return false;
    }
    if (started_) {
        return true;
    }
    if (DjiCore_ApplicationStart() != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        std::fprintf(stderr, "DjiCore_ApplicationStart failed\n");
        return false;
    }
    started_ = true;
    return true;
}

void PsdkLifecycle::Shutdown() {
    if (initialized_) {
        DjiCore_DeInit();
    }
    started_ = false;
    initialized_ = false;
}

} // namespace manifold3
