// SPDX-License-Identifier: Apache-2.0
//
// Neam Utility - AWS Signature V4 (v0.6.0)
//

#pragma once
#ifdef NEAM_BACKEND_AWS

#include <string>
#include <unordered_map>

namespace neamc::util
{

struct AwsCredentials
{
  std::string access_key_id;
  std::string secret_access_key;
  std::string session_token;
};

/// Load AWS credentials from environment or instance metadata
AwsCredentials load_aws_credentials();

/// Sign an HTTP request with AWS Signature V4
/// Returns headers to add (Authorization, X-Amz-Date, etc.)
std::unordered_map<std::string, std::string> sign_aws_request(
    const std::string& method, const std::string& url, const std::string& body,
    const std::string& service, const std::string& region,
    const AwsCredentials& credentials,
    const std::unordered_map<std::string, std::string>& extra_headers = {});

}  // namespace neamc::util

#endif  // NEAM_BACKEND_AWS
