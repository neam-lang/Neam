# Neam Package Ecosystem Design

A comprehensive package management system for Neam, inspired by pip (Python), cargo (Rust), and npm (Node.js).

## Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                     NEAM PACKAGE ECOSYSTEM                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────┐    ┌──────────────┐    ┌───────────────────────┐  │
│  │ neam-pkg │───▶│ registry.neam│───▶│  Package Storage      │  │
│  │   CLI    │    │    .dev      │    │  (S3/GCS/CloudFlare)  │  │
│  └──────────┘    └──────────────┘    └───────────────────────┘  │
│       │                                         │                │
│       ▼                                         ▼                │
│  ┌──────────┐    ┌──────────────┐    ┌───────────────────────┐  │
│  │ neam.toml│    │  neam.lock   │    │  ~/.neam/packages/    │  │
│  │ manifest │    │  lock file   │    │  (local cache)        │  │
│  └──────────┘    └──────────────┘    └───────────────────────┘  │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Components

### 1. Package Manager CLI (`neam-pkg`)

```bash
# Install packages
neam-pkg install                    # Install all dependencies from neam.toml
neam-pkg install agent-utils        # Install specific package
neam-pkg install agent-utils@1.2.0  # Install specific version
neam-pkg install --dev test-utils   # Install as dev dependency

# Package management
neam-pkg update                     # Update all packages
neam-pkg update agent-utils         # Update specific package
neam-pkg remove agent-utils         # Remove package
neam-pkg list                       # List installed packages
neam-pkg outdated                   # Show outdated packages

# Publishing
neam-pkg login                      # Login to registry
neam-pkg publish                    # Publish package to registry
neam-pkg yank 1.0.0                 # Yank a version (deprecate)

# Search and info
neam-pkg search rag                 # Search packages
neam-pkg info agent-utils           # Show package info
neam-pkg docs agent-utils           # Open package documentation

# Local development
neam-pkg link                       # Link local package for development
neam-pkg unlink                     # Unlink local package
```

### 2. Package Registry (`registry.neam.dev`)

Central repository for Neam packages with:
- Package metadata and versions
- Download statistics
- Documentation hosting
- Search API
- User authentication

### 3. Package Format (`.neampkg`)

Compressed archive containing:
```
my-package-1.0.0.neampkg
├── neam.toml           # Package manifest
├── README.md           # Documentation
├── LICENSE             # License file
├── src/                # Source code
│   ├── lib.neam        # Library entry point
│   └── ...
├── checksums.sha256    # File checksums
└── signature.sig       # Optional: Package signature
```

---

## Detailed Design

### Package Manifest (`neam.toml`)

```toml
[package]
name = "agent-utils"
version = "1.0.0"
description = "Utility functions for Neam agents"
authors = ["Developer <dev@example.com>"]
license = "MIT"
repository = "https://github.com/neam-lang/agent-utils"
documentation = "https://docs.neam.dev/agent-utils"
homepage = "https://neam.dev/packages/agent-utils"
readme = "README.md"
keywords = ["agents", "utilities", "helpers"]
categories = ["agent-development", "utilities"]

# Minimum Neam version required
neam = ">=1.0.0"

# Entry points
[lib]
path = "src/lib.neam"

# Optional binaries
[[bin]]
name = "agent-gen"
path = "src/bin/generator.neam"

# Dependencies
[dependencies]
base-utils = "^1.0.0"
json-parser = { version = "2.0", features = ["streaming"] }

[dev-dependencies]
test-framework = "0.5.0"

# Optional features
[features]
default = ["basic"]
basic = []
advanced = ["rag-support"]
rag-support = ["vector-store"]

# Feature-specific dependencies
[dependencies.vector-store]
optional = true
version = "1.0.0"
```

### Registry API

```yaml
# API Endpoints
Base URL: https://registry.neam.dev/api/v1

# Authentication
POST   /auth/login              # Login, get token
POST   /auth/register           # Register new account
POST   /auth/token              # Generate API token

# Packages
GET    /packages                # List all packages
GET    /packages/search?q=      # Search packages
GET    /packages/{name}         # Get package info
GET    /packages/{name}/{ver}   # Get specific version
GET    /packages/{name}/versions # List all versions
POST   /packages                # Publish new package
DELETE /packages/{name}/{ver}   # Yank version

# Downloads
GET    /download/{name}/{ver}   # Download package

# Users
GET    /users/{username}        # Get user profile
GET    /users/{username}/packages # User's packages

# Statistics
GET    /stats/popular           # Popular packages
GET    /stats/recent            # Recently updated
GET    /stats/{name}/downloads  # Download stats
```

### Directory Structure

```
~/.neam/
├── config.toml           # Global configuration
├── credentials.toml      # Registry credentials (encrypted)
├── cache/
│   ├── registry/         # Cached registry index
│   │   └── index.json
│   └── packages/         # Downloaded package cache
│       ├── agent-utils-1.0.0.neampkg
│       └── ...
└── packages/             # Installed packages (global)
    ├── agent-utils/
    │   └── 1.0.0/
    │       ├── src/
    │       └── neam.toml
    └── ...

# Project local
my-project/
├── neam.toml
├── neam.lock
└── .neam/
    └── packages/         # Project-local packages
        └── ...
```

---

## Implementation Plan

### Phase 1: Core Package Manager

1. **Package format specification**
2. **Local package installation**
3. **Dependency resolution algorithm**
4. **Lock file generation**

### Phase 2: Registry Integration

1. **Registry server implementation**
2. **Package publishing workflow**
3. **Authentication system**
4. **Search functionality**

### Phase 3: Advanced Features

1. **Workspace support**
2. **Private registries**
3. **Package signing**
4. **Audit and security scanning**

---

## Comparison with Other Ecosystems

| Feature | neam-pkg | pip | cargo | npm |
|---------|----------|-----|-------|-----|
| Manifest | neam.toml | setup.py/pyproject.toml | Cargo.toml | package.json |
| Lock file | neam.lock | requirements.txt | Cargo.lock | package-lock.json |
| Registry | registry.neam.dev | pypi.org | crates.io | npmjs.com |
| Install cmd | neam-pkg install | pip install | cargo add | npm install |
| Publish cmd | neam-pkg publish | twine upload | cargo publish | npm publish |
| Global cache | ~/.neam/cache | ~/.cache/pip | ~/.cargo | ~/.npm |

---

## Security Considerations

1. **Package signing** - Optional GPG signatures
2. **Checksum verification** - SHA256 for all files
3. **Dependency auditing** - Security vulnerability database
4. **Two-factor auth** - For package publishers
5. **Typosquatting protection** - Name similarity checks

---

## Registry Database Schema

```sql
-- Users
CREATE TABLE users (
    id UUID PRIMARY KEY,
    username VARCHAR(64) UNIQUE NOT NULL,
    email VARCHAR(255) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    created_at TIMESTAMP DEFAULT NOW(),
    verified BOOLEAN DEFAULT FALSE
);

-- Packages
CREATE TABLE packages (
    id UUID PRIMARY KEY,
    name VARCHAR(64) UNIQUE NOT NULL,
    owner_id UUID REFERENCES users(id),
    description TEXT,
    repository VARCHAR(512),
    documentation VARCHAR(512),
    homepage VARCHAR(512),
    created_at TIMESTAMP DEFAULT NOW(),
    updated_at TIMESTAMP DEFAULT NOW(),
    downloads BIGINT DEFAULT 0
);

-- Versions
CREATE TABLE versions (
    id UUID PRIMARY KEY,
    package_id UUID REFERENCES packages(id),
    version VARCHAR(64) NOT NULL,
    neam_version VARCHAR(32),
    checksum VARCHAR(64) NOT NULL,
    size_bytes BIGINT,
    published_at TIMESTAMP DEFAULT NOW(),
    yanked BOOLEAN DEFAULT FALSE,
    downloads BIGINT DEFAULT 0,
    UNIQUE(package_id, version)
);

-- Dependencies
CREATE TABLE dependencies (
    id UUID PRIMARY KEY,
    version_id UUID REFERENCES versions(id),
    dependency_name VARCHAR(64) NOT NULL,
    version_req VARCHAR(64) NOT NULL,
    optional BOOLEAN DEFAULT FALSE,
    features TEXT[] DEFAULT '{}'
);

-- Keywords
CREATE TABLE keywords (
    id UUID PRIMARY KEY,
    keyword VARCHAR(64) UNIQUE NOT NULL
);

CREATE TABLE package_keywords (
    package_id UUID REFERENCES packages(id),
    keyword_id UUID REFERENCES keywords(id),
    PRIMARY KEY (package_id, keyword_id)
);

-- API Tokens
CREATE TABLE api_tokens (
    id UUID PRIMARY KEY,
    user_id UUID REFERENCES users(id),
    name VARCHAR(64),
    token_hash VARCHAR(255) NOT NULL,
    created_at TIMESTAMP DEFAULT NOW(),
    last_used_at TIMESTAMP,
    expires_at TIMESTAMP
);
```

---

## Example Workflow

### Publishing a Package

```bash
# 1. Create package structure
mkdir agent-utils && cd agent-utils
neam-pkg init --lib

# 2. Edit neam.toml
# 3. Write code in src/lib.neam

# 4. Login to registry
neam-pkg login
# Enter username: developer
# Enter password: ********
# Logged in as developer

# 5. Publish
neam-pkg publish
# Publishing agent-utils v1.0.0
# Compressing package...
# Uploading to registry.neam.dev...
# Published! https://registry.neam.dev/packages/agent-utils
```

### Installing a Package

```bash
# 1. Add dependency
neam-pkg install agent-utils

# Output:
# Resolving dependencies...
# Downloading agent-utils v1.0.0
# Downloading base-utils v1.2.0 (dependency)
# Installing agent-utils v1.0.0
# Installing base-utils v1.2.0
# Updated neam.lock
# Done! Installed 2 packages

# 2. Use in code
# src/main.neam
import agent_utils

{
  let result = agent_utils.helper_function();
  emit result;
}
```

---

## Configuration Files

### Global Config (`~/.neam/config.toml`)

```toml
[registry]
default = "https://registry.neam.dev"

# Private registries
[[registry.private]]
name = "company"
url = "https://neam.company.com"
token-env = "COMPANY_NEAM_TOKEN"

[install]
# Install location
global-dir = "~/.neam/packages"
# Number of parallel downloads
parallel = 4
# Timeout in seconds
timeout = 60

[build]
# Default profile
profile = "release"
# Number of parallel jobs
jobs = 4

[net]
# Proxy settings
# proxy = "http://proxy.company.com:8080"
# Offline mode
offline = false
```

### Credentials (`~/.neam/credentials.toml`)

```toml
# Encrypted with system keyring
[registries.default]
token = "encrypted:..."

[registries.company]
token = "encrypted:..."
```
