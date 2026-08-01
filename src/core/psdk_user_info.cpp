#include "core/psdk_user_info.h"

#include <cstdio>
#include <cstring>

namespace manifold3 {

namespace {

bool IsPlaceholder(const char *value, const char *placeholder) {
    return std::strcmp(value, placeholder) == 0;
}

} // namespace

bool FillPsdkUserInfo(const PsdkCredentialStrings &credentials, T_DjiUserInfo *user_info) {
    if (user_info == nullptr) {
        std::fprintf(stderr, "PSDK user info output pointer is null\n");
        return false;
    }
    if (credentials.app_name == nullptr || credentials.app_id == nullptr || credentials.app_key == nullptr ||
        credentials.app_license == nullptr || credentials.developer_account == nullptr ||
        credentials.baud_rate == nullptr) {
        std::fprintf(stderr, "PSDK user info string is null\n");
        return false;
    }

    std::memset(user_info, 0, sizeof(*user_info));

    if (std::strlen(credentials.app_name) >= sizeof(user_info->appName) ||
        std::strlen(credentials.app_id) > sizeof(user_info->appId) ||
        std::strlen(credentials.app_key) > sizeof(user_info->appKey) ||
        std::strlen(credentials.app_license) > sizeof(user_info->appLicense) ||
        std::strlen(credentials.developer_account) >= sizeof(user_info->developerAccount) ||
        std::strlen(credentials.baud_rate) > sizeof(user_info->baudRate)) {
        std::fprintf(stderr, "PSDK user info string exceeds field limits\n");
        return false;
    }

    if (IsPlaceholder(credentials.app_name, "your_app_name") || IsPlaceholder(credentials.app_id, "your_app_id") ||
        IsPlaceholder(credentials.app_key, "your_app_key") ||
        IsPlaceholder(credentials.app_license, "your_app_license") ||
        IsPlaceholder(credentials.developer_account, "your_developer_account")) {
        std::fprintf(stderr, "PSDK credentials not configured; pass MANIFOLD3_APP_ID / "
                             "MANIFOLD3_APP_KEY / MANIFOLD3_APP_NAME / "
                             "MANIFOLD3_APP_LICENSE / MANIFOLD3_DEVELOPER_ACCOUNT to CMake\n");
        return false;
    }

    std::strncpy(user_info->appName, credentials.app_name, sizeof(user_info->appName) - 1);
    std::memcpy(user_info->appId, credentials.app_id, std::strlen(credentials.app_id));
    std::memcpy(user_info->appKey, credentials.app_key, std::strlen(credentials.app_key));
    std::memcpy(user_info->appLicense, credentials.app_license, std::strlen(credentials.app_license));
    std::memcpy(user_info->baudRate, credentials.baud_rate, std::strlen(credentials.baud_rate));
    std::strncpy(user_info->developerAccount, credentials.developer_account, sizeof(user_info->developerAccount) - 1);
    return true;
}

} // namespace manifold3
