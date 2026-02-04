// SPDX-License-Identifier: Apache-2.0
//
// Neam Utility - GCP Authentication (v0.6.0)
//

#pragma once
#ifdef NEAM_BACKEND_GCP

#include <string>

namespace neamc::util
{

/// Get GCP access token via Application Default Credentials.
/// Reads service account JSON from GOOGLE_APPLICATION_CREDENTIALS env var
/// and exchanges it for a short-lived OAuth2 access token.
std::string get_gcp_access_token();

/// Get GCP project ID from service account JSON or metadata server
std::string get_gcp_project_id();

}  // namespace neamc::util

#endif  // NEAM_BACKEND_GCP
