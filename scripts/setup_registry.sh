#!/usr/bin/env bash

set -euo pipefail

registry_root="${HOME}/.neam"
registry_dir="${registry_root}/registry"
traces_dir="${registry_root}/traces"
config_file="${registry_root}/config.json"

mkdir -p "${registry_dir}/agents" \
  "${registry_dir}/packages" \
  "${registry_dir}/receipts" \
  "${registry_dir}/schemas" \
  "${traces_dir}"

chmod 755 "${registry_root}" "${registry_dir}" \
  "${registry_dir}/agents" \
  "${registry_dir}/packages" \
  "${registry_dir}/receipts" \
  "${registry_dir}/schemas"
chmod 700 "${traces_dir}"

if [[ ! -f "${config_file}" ]]; then
  cat <<'CONFIG' > "${config_file}"
{
  "registry": {
    "path": "~/.neam/registry",
    "default_namespace": "local"
  },
  "telemetry": {
    "traces_path": "~/.neam/traces",
    "enabled": true
  }
}
CONFIG
fi

echo "Neam registry initialized at ${registry_dir}"
