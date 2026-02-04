# Changelog

All notable changes to the Neam project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.6.0] - 2026-01-30

### Added
- **Cloud-Native Deployment**: Distributed state backends (PostgreSQL, Redis, etcd)
- **LLM Gateway**: Centralized provider routing with rate limiting, cost tracking, and circuit breakers
- **OpenTelemetry Integration**: Full tracing and metrics export with OTel SDK
- **Health Management**: Health check endpoints with readiness and liveness probes
- **Multi-Cloud Deployment**: Terraform, Helm, AWS ECS/Lambda, GCP Cloud Run, Azure Container Apps/AKS
- **Docker Support**: Multi-stage Dockerfile generation with `neamc deploy docker`
- **Kubernetes Support**: Native K8s manifest generation with HPA, ConfigMaps, Secrets
- **Telemetry Dashboard**: Built-in metrics collection and export for monitoring
- **Evaluation Checklist**: 198 test cases for cloud-native deployment validation

### Changed
- State backend architecture refactored to support distributed backends
- Configuration system extended with deployment-specific options in `neam.toml`

## [0.5.0] - 2026-01-15

### Added
- **Cognitive Features**: Reasoning strategies (chain-of-thought, tree-of-thought, reflection)
- **Learning Loop**: Experience replay with SQLite-backed memory
- **Prompt Evolution**: Autonomous prompt refinement with rollback support
- **Autonomous Agents**: Goal-driven agents with triggers, budgets, and self-monitoring
- **Metacognition**: Confidence scoring and self-assessment capabilities

### Changed
- Agent runtime extended with cognitive state management
- Memory subsystem enhanced with learning episode storage

## [0.4.0] - 2025-12-20

### Added
- **A2A Protocol**: Agent-to-Agent communication following Google's A2A specification
- **Agent Discovery**: Service registry with capability-based agent lookup
- **Evaluation Framework (neam-gym)**: Agent evaluation with multiple grader types
- **Voice Pipeline**: Speech-to-Text (Whisper, Gemini) and Text-to-Speech (OpenAI, Gemini)
- **Vision Support**: Image analysis capabilities via multi-modal LLM providers
- **DAP Debugger**: Debug Adapter Protocol support for IDE integration

### Changed
- Agent orchestration system refactored for multi-agent mesh support
- Streaming architecture improved for voice pipeline latency requirements

## [0.3.0] - 2025-11-15

### Added
- **Package Manager (neam-pkg)**: Install, publish, and manage Neam packages
- **Module System**: Import resolution, visibility rules, and circular dependency detection
- **neam.toml Configuration**: Project manifest with dependency management
- **Package Registry**: Centralized registry for package discovery and distribution
- **C API**: Foreign function interface for embedding Neam in C/C++ applications
- **Python Bindings**: ctypes-based Python package for Neam interop
- **Node.js Bindings**: FFI-based Node.js package for Neam interop
- **API Server (neam-api)**: HTTP server for deploying agents as REST endpoints

### Changed
- Build system restructured to produce shared library (libneam)

## [0.2.0] - 2025-10-01

### Added
- **Multi-Agent Orchestration**: Handoffs, parallel execution, and hierarchical agents
- **Guardrails**: Input validation, output safety checks, PII detection patterns
- **Runner System**: Configurable agent execution with max_turns, tracing, and error recovery
- **Tool Use**: Function calling with MCP (Model Context Protocol) integration
- **Streaming**: Token-by-token streaming with backpressure support
- **Multi-Provider Support**: OpenAI, Anthropic, Gemini, Ollama, Azure, Bedrock, Vertex AI
- **Observability**: Built-in tracing, cost tracking, and latency metrics
- **Async/Await**: Concurrent agent execution with future composition
- **SQLite Persistence**: Conversation memory and state persistence

### Changed
- VM extended with agent-aware instruction set
- Compiler enhanced with agent and skill declaration parsing

## [0.1.0] - 2025-08-15

### Added
- **Core Language**: Variables, functions, control flow, loops, error handling
- **Type System**: Numbers, strings, booleans, lists, maps, nil, options
- **RAG Pipeline**: Document ingestion, chunking, embedding, vector search, re-ranking
- **Agent Declarations**: Provider, model, system prompt, temperature configuration
- **Skill System**: Reusable agent capabilities with parameter schemas
- **Knowledge Bases**: Vector store integration for retrieval-augmented generation
- **Compiler**: Neam source to bytecode compilation (`.neam` -> `.neamb`)
- **Virtual Machine**: Stack-based bytecode execution engine
- **REPL (neam-cli)**: Interactive development environment
- **LSP Server**: Language Server Protocol for IDE support (VS Code)
- **Mark-Sweep GC**: Automatic garbage collection for heap objects

[0.6.0]: https://github.com/neam-lang/Neam/releases/tag/v0.6.0
[0.5.0]: https://github.com/neam-lang/Neam/releases/tag/v0.5.0
[0.4.0]: https://github.com/neam-lang/Neam/releases/tag/v0.4.0
[0.3.0]: https://github.com/neam-lang/Neam/releases/tag/v0.3.0
[0.2.0]: https://github.com/neam-lang/Neam/releases/tag/v0.2.0
[0.1.0]: https://github.com/neam-lang/Neam/releases/tag/v0.1.0
