#include <dji_core.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

#include "core/psdk_user_info.h"

using manifold3::FillPsdkUserInfo;
using manifold3::PsdkCredentialStrings;

namespace {

constexpr char kAppName[] = "vision-detect";
constexpr char kAppId[] = "123456";
constexpr char kAppKey[] = "a1b2c3d4e5f6a7b8c9d0";
constexpr char kAppLicense[] = "license-0001";
constexpr char kDeveloperAccount[] = "dev@example.com";
constexpr char kBaudRate[] = "460800";

PsdkCredentialStrings MakeCredentials() {
    return {kAppName, kAppId, kAppKey, kAppLicense, kDeveloperAccount, kBaudRate};
}

// Returns true when the given field (index into PsdkCredentialStrings) is null.
bool FillsWithNullMember(int field, T_DjiUserInfo *info) {
    PsdkCredentialStrings creds = MakeCredentials();
    switch (field) {
    case 0:
        creds.app_name = nullptr;
        break;
    case 1:
        creds.app_id = nullptr;
        break;
    case 2:
        creds.app_key = nullptr;
        break;
    case 3:
        creds.app_license = nullptr;
        break;
    case 4:
        creds.developer_account = nullptr;
        break;
    case 5:
        creds.baud_rate = nullptr;
        break;
    default:
        break;
    }
    return FillPsdkUserInfo(creds, info);
}

} // namespace

int main() {
    // Ordinary valid values succeed and copy correctly.
    {
        T_DjiUserInfo info;
        assert(FillPsdkUserInfo(MakeCredentials(), &info));
        assert(std::strcmp(info.appName, kAppName) == 0);
        assert(std::strcmp(info.appId, kAppId) == 0);
        assert(std::strcmp(info.appKey, kAppKey) == 0);
        assert(std::strcmp(info.appLicense, kAppLicense) == 0);
        assert(std::strcmp(info.developerAccount, kDeveloperAccount) == 0);
        assert(std::strcmp(info.baudRate, kBaudRate) == 0);
        // Struct was zeroed first, so the byte after each short fixed field is NUL.
        assert(info.appId[std::strlen(kAppId)] == '\0');
        assert(info.appKey[std::strlen(kAppKey)] == '\0');
        assert(info.appLicense[std::strlen(kAppLicense)] == '\0');
        assert(info.baudRate[std::strlen(kBaudRate)] == '\0');
    }

    // Full-length fixed fields keep their last byte.
    {
        const std::string appId(16, 'a');
        const std::string appKey(32, 'b');
        const std::string appLicense(512, 'c');
        const std::string baudRate(7, 'd');
        PsdkCredentialStrings creds = {kAppName,           appId.c_str(),     appKey.c_str(),
                                       appLicense.c_str(), kDeveloperAccount, baudRate.c_str()};
        T_DjiUserInfo info;
        assert(FillPsdkUserInfo(creds, &info));
        assert(std::memcmp(info.appId, appId.data(), 16) == 0);
        assert(std::memcmp(info.appKey, appKey.data(), 32) == 0);
        assert(std::memcmp(info.appLicense, appLicense.data(), 512) == 0);
        assert(std::memcmp(info.baudRate, baudRate.data(), 7) == 0);
    }

    // Near-full string fields stay NUL-terminated in the last array byte.
    {
        const std::string appName(31, 'e');
        const std::string developerAccount(63, 'f');
        PsdkCredentialStrings creds = {appName.c_str(),          kAppId,   kAppKey, kAppLicense,
                                       developerAccount.c_str(), kBaudRate};
        T_DjiUserInfo info;
        assert(FillPsdkUserInfo(creds, &info));
        assert(std::memcmp(info.appName, appName.data(), 31) == 0);
        assert(info.appName[31] == '\0');
        assert(std::memcmp(info.developerAccount, developerAccount.data(), 63) == 0);
        assert(info.developerAccount[63] == '\0');
    }

    // One byte over the limit fails for every field.
    {
        const std::string appName(32, 'g');
        const std::string appId(17, 'h');
        const std::string appKey(33, 'i');
        const std::string appLicense(513, 'j');
        const std::string developerAccount(64, 'k');
        const std::string baudRate(8, 'l');
        struct {
            PsdkCredentialStrings creds;
        } overLimit[] = {
            {{appName.c_str(), kAppId, kAppKey, kAppLicense, kDeveloperAccount, kBaudRate}},
            {{kAppName, appId.c_str(), kAppKey, kAppLicense, kDeveloperAccount, kBaudRate}},
            {{kAppName, kAppId, appKey.c_str(), kAppLicense, kDeveloperAccount, kBaudRate}},
            {{kAppName, kAppId, kAppKey, appLicense.c_str(), kDeveloperAccount, kBaudRate}},
            {{kAppName, kAppId, kAppKey, kAppLicense, developerAccount.c_str(), kBaudRate}},
            {{kAppName, kAppId, kAppKey, kAppLicense, kDeveloperAccount, baudRate.c_str()}},
        };
        for (const auto &over : overLimit) {
            T_DjiUserInfo info;
            assert(!FillPsdkUserInfo(over.creds, &info));
        }
    }

    // Placeholder values fail.
    {
        constexpr char kPlaceholders[][24] = {"your_app_name", "your_app_id", "your_app_key", "your_app_license",
                                              "your_developer_account"};
        for (const char *placeholder : kPlaceholders) {
            PsdkCredentialStrings creds = {kAppName, kAppId, kAppKey, kAppLicense, kDeveloperAccount, kBaudRate};
            if (std::strcmp(placeholder, "your_app_name") == 0) {
                creds.app_name = placeholder;
            } else if (std::strcmp(placeholder, "your_app_id") == 0) {
                creds.app_id = placeholder;
            } else if (std::strcmp(placeholder, "your_app_key") == 0) {
                creds.app_key = placeholder;
            } else if (std::strcmp(placeholder, "your_app_license") == 0) {
                creds.app_license = placeholder;
            } else {
                creds.developer_account = placeholder;
            }
            T_DjiUserInfo info;
            assert(!FillPsdkUserInfo(creds, &info));
        }
    }

    // Null arguments fail.
    {
        assert(!FillPsdkUserInfo(MakeCredentials(), nullptr));
        for (int field = 0; field < 6; ++field) {
            T_DjiUserInfo info;
            assert(!FillsWithNullMember(field, &info));
        }
    }

    return 0;
}
