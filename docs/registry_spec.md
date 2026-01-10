# Neam Package Registry (Local) Specification

This document defines the local registry layout and artifacts that Neam tooling
must read and emit. The Neam root repository does not implement registry logic;
it defines the rules and interfaces that the Neam compiler/runtime must adhere
when publishing and resolving local registry packages.

## Directory layout

Local registries are stored under the user home directory:

```
~/.neam/registry/${AGENT_NAME}/${VERSION}/
```

Each version directory is immutable once published.

## Required artifacts

Every version directory must contain the following artifacts:

### `agent.npk`

A zip archive containing the `.neam` source for the agent package. The archive
is the canonical payload consumed by the compiler and runtime for install or
execution workflows.

### `gym_score.json`

The certificate of competence emitted by the evaluation pipeline. It must
include a `pass_rate` field with a value of `1.0` to be considered valid for
publication.

### `manifest.json`

Metadata for the registry entry, including:

- `author`
- `description`
- `latest_version`

The manifest must point to the latest published version for the agent name,
allowing tooling to resolve the newest compatible package.
