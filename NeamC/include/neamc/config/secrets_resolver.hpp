// SPDX-License-Identifier: Apache-2.0
//
// Neam v0.6.0 - Secrets Resolver Interface and Implementations
//

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace neamc::config
{

// Base interface for secrets resolution
class SecretsResolver
{
public:
  virtual ~SecretsResolver() = default;

  // Resolve a secret URI to its plaintext value
  virtual std::string resolve(const std::string& uri) = 0;

  // Check if this resolver can handle the given URI scheme
  virtual bool can_resolve(const std::string& uri) const = 0;
};

// Resolves secrets from environment variables
// URI format: env://VARIABLE_NAME
class EnvSecretsResolver : public SecretsResolver
{
public:
  std::string resolve(const std::string& uri) override;
  bool can_resolve(const std::string& uri) const override;
};

#ifdef NEAM_BACKEND_AWS
// Resolves secrets from AWS Secrets Manager via libcurl
// URI format: aws-sm://secret-name
class AwsSecretsResolver : public SecretsResolver
{
public:
  explicit AwsSecretsResolver(const std::string& region = "us-east-1");
  std::string resolve(const std::string& uri) override;
  bool can_resolve(const std::string& uri) const override;

private:
  std::string region_;
};
#endif

#ifdef NEAM_BACKEND_AZURE
// Resolves secrets from Azure Key Vault via libcurl
// URI format: azure-kv://vault-name/secret-name
class AzureKeyVaultResolver : public SecretsResolver
{
public:
  explicit AzureKeyVaultResolver(const std::string& vault_url = "");
  std::string resolve(const std::string& uri) override;
  bool can_resolve(const std::string& uri) const override;

private:
  std::string vault_url_;
};
#endif

#ifdef NEAM_BACKEND_GCP
// Resolves secrets from GCP Secret Manager via libcurl
// URI format: gcp-sm://project/secret-name
class GcpSecretsResolver : public SecretsResolver
{
public:
  explicit GcpSecretsResolver(const std::string& project = "");
  std::string resolve(const std::string& uri) override;
  bool can_resolve(const std::string& uri) const override;

private:
  std::string project_;
};
#endif

// Resolves secrets from HashiCorp Vault via HTTPS
// URI format: vault://path/to/secret#key
class VaultSecretsResolver : public SecretsResolver
{
public:
  explicit VaultSecretsResolver(const std::string& vault_addr = "",
                                const std::string& vault_token = "");
  std::string resolve(const std::string& uri) override;
  bool can_resolve(const std::string& uri) const override;

private:
  std::string vault_addr_;
  std::string vault_token_;
};

// Resolves secrets from Kubernetes mounted secret files
// URI format: k8s://namespace/secret-name/key
class K8sSecretsResolver : public SecretsResolver
{
public:
  explicit K8sSecretsResolver(const std::string& mount_path = "/var/run/secrets");
  std::string resolve(const std::string& uri) override;
  bool can_resolve(const std::string& uri) const override;

private:
  std::string mount_path_;
};

// Tries each resolver in order until one succeeds
class CompositeSecretsResolver : public SecretsResolver
{
public:
  void add_resolver(std::unique_ptr<SecretsResolver> resolver);
  std::string resolve(const std::string& uri) override;
  bool can_resolve(const std::string& uri) const override;

private:
  std::vector<std::unique_ptr<SecretsResolver>> resolvers_;
};

// Factory: create a resolver chain based on provider string
// Supported: "env", "aws", "azure", "gcp", "vault", "k8s", "composite"
std::unique_ptr<SecretsResolver> create_secrets_resolver(const std::string& provider);

}  // namespace neamc::config
