# Contributing to Neam

Thank you for your interest in contributing to Neam! This guide will help you get started.

## Branching Strategy

We use **GitHub Flow** - a simple, lightweight branching model.

### Main Branch

- `main` is always stable and releasable
- All releases are tagged from `main` (e.g., `v0.1`, `v0.2`)
- Direct commits to `main` are discouraged; use pull requests

### Branch Naming Convention

Use descriptive prefixes for your branches:

| Prefix | Purpose | Example |
|--------|---------|---------|
| `feature/` | New features | `feature/cli-tab-completion` |
| `fix/` | Bug fixes | `fix/parser-crash-on-empty-input` |
| `docs/` | Documentation | `docs/update-api-reference` |
| `refactor/` | Code refactoring | `refactor/vm-memory-management` |
| `perf/` | Performance improvements | `perf/compiler-optimization` |
| `test/` | Test additions/fixes | `test/add-repl-tests` |

### Component-Specific Branches

For larger features, include the component:

```
feature/compiler-type-inference
feature/vm-async-support
feature/cli-syntax-highlighting
feature/lsp-hover-info
feature/pkg-dependency-resolution
feature/api-websocket-support
```

## Development Workflow

### 1. Fork and Clone

```bash
gh repo fork neam-lang/Neam --clone
cd Neam
```

### 2. Create a Feature Branch

```bash
git checkout -b feature/your-feature-name
```

### 3. Make Changes

- Write clean, readable code
- Follow existing code style
- Add tests for new functionality
- Update documentation if needed

### 4. Build and Test

```bash
# Build
./mac_build_script.sh -s

# Test specific binary
./build/neamc --help
./build/neam-cli --help
```

### 5. Commit Changes

Write clear, concise commit messages:

```bash
# Good commit messages
git commit -m "Add tab completion for REPL commands"
git commit -m "Fix parser crash when input contains unicode"
git commit -m "Improve VM memory allocation performance"

# Bad commit messages
git commit -m "Fixed stuff"
git commit -m "WIP"
git commit -m "Changes"
```

### 6. Push and Create Pull Request

```bash
git push -u origin feature/your-feature-name
gh pr create --title "Add tab completion for REPL commands" --body "Description of changes"
```

## Pull Request Guidelines

### PR Title Format

```
<type>: <short description>

Examples:
feat: Add tab completion for REPL commands
fix: Resolve parser crash on empty input
docs: Update installation instructions
refactor: Simplify VM bytecode dispatch
perf: Optimize string concatenation
test: Add unit tests for compiler
```

### PR Description Template

```markdown
## Summary
Brief description of what this PR does.

## Changes
- Change 1
- Change 2

## Testing
How was this tested?

## Checklist
- [ ] Code builds without errors
- [ ] Tests pass
- [ ] Documentation updated (if needed)
```

## Code Style

### C++ (NeamC)

- Use 2-space indentation
- Opening braces on same line
- Use `snake_case` for functions and variables
- Use `PascalCase` for classes and types
- Use `UPPER_CASE` for constants

```cpp
// Good
class TokenParser {
public:
  void parse_expression(const std::string& input) {
    if (input.empty()) {
      return;
    }
    // ...
  }

private:
  static constexpr int MAX_TOKENS = 1000;
};
```

### Neam Code

- Use 2-space indentation
- Use `snake_case` for variables and functions
- Use `PascalCase` for agents and types

```neam
agent DataAnalyzer {
  provider: "openai"
  model: "gpt-4"

  skill analyze_data {
    description: "Analyze input data"
    params: [data: string]
    impl(data) {
      let result = process(data);
      return result;
    }
  }
}
```

## Project Structure

```
Neam/
├── NeamC/                 # C++ compiler and tools
│   ├── src/
│   │   ├── neamc/         # Compiler main
│   │   ├── vm/            # Virtual machine
│   │   ├── lsp/           # Language server
│   │   ├── dap/           # Debug adapter
│   │   └── pkg/           # Package manager
│   └── include/
├── stdlib/                # Standard library
├── examples/              # Example programs
├── dist/                  # Distribution packages
└── docs/                  # Documentation
```

## Getting Help

- Open an issue for bugs or feature requests
- Start a discussion for questions
- Check existing issues before creating new ones

## License

By contributing to Neam, you agree that your contributions will be licensed under the same license as the project.
