# Phase 20 Project System - Implementation Notes

This document captures the Phase 20 project management surface for Neam. It
mirrors the v1.0 specification draft for project structure, manifests, workspace
support, dependency management, build pipelines, templates, and CI/CD
integration. The goal is to provide a cohesive stdlib scaffold that the runtime
and compiler can wire to concrete behavior.

## Scope

The following stdlib namespaces now expose Phase 20 scaffolding:

- `std.project.manifest` (manifest parsing, defaults, schema validation, workspace inheritance)
- `std.project.workspace` (workspace discovery, dependency graph, workspace commands)
- `std.project.dependencies` (resolution, registry, lockfiles, sources, caching)
- `std.project.build` (compiler integration, bundling, profiles, targets, hooks)
- `std.project.templates` (template registry, scaffolding, interpolation, built-ins)
- `std.project.cli` (command parsing, output helpers, command implementations)
- `std.project.deploy` (deployment targets, generators, environments)
- `std.project.cicd` (pipeline templates, stages, artifacts)
- `std.project.security` (audit, secrets scanning, SAST, license checks)
- `std.project.testing` (mocks, fixtures, assertions)

## Design Notes

- Interfaces use structured maps with helper constructors to keep the surface
  consistent with existing stdlib patterns.
- Parsing/validation is split between manifest schema checks and runtime-backed
  TOML parsing to keep the stdlib implementation deterministic.
- Workspace inheritance is handled as a merge of workspace defaults into member
  manifests, with explicit overrides preserved.
- Dependency resolution is abstracted behind registry and source adapters so the
  runtime can implement actual fetch/lock logic.

## Runtime Hooks

Phase 20 introduces runtime hooks that the stdlib expects to call. The runtime
and compiler should provide these builtins:

| Builtin | Module | Description |
| --- | --- | --- |
| `project_manifest_parse(toml)` | `std.project.manifest.parser` | Parse a neam.toml string into a manifest map. |
| `project_manifest_stringify(manifest)` | `std.project.manifest.parser` | Serialize a manifest map to TOML. |
| `project_manifest_validate(manifest)` | `std.project.manifest.schema` | Validate a manifest against the schema. |
| `project_workspace_discover(root, patterns)` | `std.project.workspace.discovery` | Discover workspace members. |
| `project_workspace_graph(members)` | `std.project.workspace.graph` | Build a dependency graph of workspace members. |
| `project_dependency_resolve(manifest, lock)` | `std.project.dependencies.resolver` | Resolve dependencies and produce lock data. |
| `project_lock_read(path)` | `std.project.dependencies.lockfile` | Read a lockfile from disk. |
| `project_lock_write(path, lock)` | `std.project.dependencies.lockfile` | Write a lockfile to disk. |
| `project_template_render(template, vars)` | `std.project.templates.interpolation` | Render template variables. |
| `project_template_fetch(source)` | `std.project.templates.registry` | Fetch template metadata or sources. |
| `project_build_compile(config)` | `std.project.build.compiler` | Invoke NeamC compilation. |
| `project_build_bundle(config)` | `std.project.build.bundler` | Produce bundle output and manifests. |
| `project_deploy_generate(target, config)` | `std.project.deploy.generators` | Generate deployment artifacts. |
| `project_cicd_template(kind)` | `std.project.cicd.pipelines` | Load CI/CD pipeline templates. |
| `project_security_audit(manifest)` | `std.project.security.audit` | Audit dependencies. |
| `project_security_scan(kind, config)` | `std.project.security.secrets` | Scan secrets/SAST. |

## Manifest Coverage

The Phase 20 manifest scaffolding covers:

- `neam-version`
- `[project]` metadata
- `[dependencies]`, `[dev-dependencies]`, `[build-dependencies]`
- `[features]`
- `[agent]`, `[knowledge]`
- `[build]`, `[profile.*]`, `[test]`, `[bench]`
- `[env]`, `[deploy]`, `[scripts]`, `[tool.*]`
- `[workspace]` with inheritance

## Next Steps

- Wire runtime implementations for TOML parsing and lockfile I/O.
- Add actual registry endpoints and auth flows in `std.project.dependencies.registry`.
- Implement real compiler/build integration inside NeamC.
- Add CLI wiring for `neam` commands to map into the stdlib handlers.
