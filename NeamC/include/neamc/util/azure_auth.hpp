// SPDX-License-Identifier: Apache-2.0
//
// Neam Utility - Azure Authentication (v0.6.0)
//

#pragma once
#ifdef NEAM_BACKEND_AZURE

#include <string>

namespace neamc::util
{

/// Get Azure AD token via managed identity or client credentials
/// Falls back to AZURE_CLIENT_ID/SECRET/TENANT env vars
std::string get_azure_token(const std::string& resource = "https://management.azure.com/");

/// Get Azure Cosmos DB key from environment
std::string get_cosmos_key();

}  // namespace neamc::util

#endif  // NEAM_BACKEND_AZURE
