// Manifold 3 PSDK application information.

// Development credentials are injected as compile definitions by the build
// system. They must not be hard-coded in the repository.
//
//   -DMANIFOLD3_APP_ID=...    -DMANIFOLD3_APP_KEY=...
//   -DMANIFOLD3_APP_NAME=...  -DMANIFOLD3_APP_LICENSE=...
//   -DMANIFOLD3_DEVELOPER_ACCOUNT=...
//
// The default placeholder values cause PsdkLifecycle::Initialize() to fail
// with a descriptive error, matching the DJI sample behavior.

#ifndef MANIFOLD3_APP_NAME
#define MANIFOLD3_APP_NAME "your_app_name"
#endif

#ifndef MANIFOLD3_APP_ID
#define MANIFOLD3_APP_ID "your_app_id"
#endif

#ifndef MANIFOLD3_APP_KEY
#define MANIFOLD3_APP_KEY "your_app_key"
#endif

#ifndef MANIFOLD3_APP_LICENSE
#define MANIFOLD3_APP_LICENSE "your_app_license"
#endif

#ifndef MANIFOLD3_DEVELOPER_ACCOUNT
#define MANIFOLD3_DEVELOPER_ACCOUNT "your_developer_account"
#endif

#ifndef MANIFOLD3_BAUD_RATE
#define MANIFOLD3_BAUD_RATE "460800"
#endif
