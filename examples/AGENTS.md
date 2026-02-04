# Project Guidelines

This file provides context for AI agents working on this project.

## Build Commands

```bash
# Build the project
cmake --build build --parallel

# Run tests
cd build && ctest --output-on-failure

# Clean build
rm -rf build && mkdir build && cd build && cmake ..
```

## Code Style

- Use 2-space indentation
- Follow modern C++17 conventions
- Prefer `const` and `constexpr` where possible
- Use snake_case for variables and functions
- Use PascalCase for types and classes
- Add documentation comments for public APIs

## Testing

- All new features require unit tests
- Tests should be in the `tests/` directory
- Use descriptive test names
- Aim for >80% code coverage

## Pull Request Guidelines

- Keep PRs focused on a single feature or fix
- Include tests for new functionality
- Update documentation as needed
- Ensure all CI checks pass before merging

## Architecture Notes

- The parser generates an AST from source code
- The compiler transforms AST to bytecode
- The VM executes bytecode instructions
- LLM providers are abstracted behind a common interface
