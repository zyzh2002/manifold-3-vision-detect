#pragma once

#include <dji_core.h>

namespace manifold3 {

// Credential strings consumed by FillPsdkUserInfo. Pointers are borrowed; the
// struct does not own or modify the pointed-to data.
struct PsdkCredentialStrings {
    const char *app_name;
    const char *app_id;
    const char *app_key;
    const char *app_license;
    const char *developer_account;
    const char *baud_rate;
};

// Validates and copies credentials into a zeroed T_DjiUserInfo. Returns false
// and prints a descriptive error to stderr when the output pointer or any
// string is null, a string exceeds its field limit, or a placeholder value is
// used. String fields (appName, developerAccount) require a trailing NUL in
// the array; fixed fields (appId, appKey, appLicense, baudRate) may use the
// full array length.
bool FillPsdkUserInfo(const PsdkCredentialStrings &credentials, T_DjiUserInfo *user_info);

} // namespace manifold3
