#!/usr/bin/env bash

set -euo pipefail

registry_root="${HOME}/.neam"
registry_dir="${registry_root}/registry"
traces_dir="${registry_root}/traces"
gym_dir="${registry_root}/gym"
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

# Set NEAM_SETUP_GYM=1 to create ~/.neam/gym for local workout history.
if [[ "${NEAM_SETUP_GYM:-0}" == "1" ]]; then
  mkdir -p "${gym_dir}"
  chmod 700 "${gym_dir}"
fi

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
