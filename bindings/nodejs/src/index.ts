/**
 * Neam Node.js Bindings
 *
 * This module provides Node.js/TypeScript bindings for the Neam
 * agentic programming language using the koffi FFI library.
 *
 * @example
 * ```typescript
 * import { Runtime } from '@neam-lang/neam';
 *
 * const runtime = new Runtime();
 * const result = runtime.run('emit "Hello from Neam!"');
 * console.log(result.output);
 * runtime.close();
 * ```
 *
 * @packageDocumentation
 */

import * as koffi from 'koffi';
import * as path from 'path';
import * as os from 'os';
import * as fs from 'fs';

// ============================================================================
// Type Definitions
// ============================================================================

/** Neam value types */
export enum ValueType {
  NIL = 0,
  BOOL = 1,
  NUMBER = 2,
  STRING = 3,
  LIST = 4,
  MAP = 5,
  FUNCTION = 6,
  AGENT = 7,
  OBJECT = 8,
}

/** Neam error types */
export enum ErrorType {
  NONE = 0,
  PARSE = 1,
  COMPILE = 2,
  RUNTIME = 3,
  TOOL = 4,
  TIMEOUT = 5,
  BUDGET = 6,
  AGENT = 7,
  MEMORY = 8,
  IO = 9,
  INVALID_ARGUMENT = 10,
}

/** Task status (A2A Protocol) */
export enum TaskStatus {
  PENDING = 0,
  RUNNING = 1,
  COMPLETED = 2,
  FAILED = 3,
  CANCELLED = 4,
}

/** Runtime configuration options */
export interface RuntimeConfig {
  /** Default LLM provider (e.g., "ollama", "openai") */
  defaultProvider?: string;
  /** Default model name */
  defaultModel?: string;
  /** Enable thread-safe mode */
  threadSafe?: boolean;
  /** Maximum call stack depth */
  maxStackDepth?: number;
  /** Allow file operations */
  enableFileIo?: boolean;
  /** Allow network operations */
  enableNetwork?: boolean;
  /** Allow shell command execution */
  enableShell?: boolean;
  /** Working directory for file operations */
  workingDirectory?: string;
}

/** Agent creation options */
export interface AgentOptions {
  /** LLM provider */
  provider?: string;
  /** Model name */
  model?: string;
  /** System prompt */
  system?: string;
  /** Temperature (0.0 to 2.0) */
  temperature?: number;
}

// ============================================================================
// Library Loading
// ============================================================================

function getLibraryExtension(): string {
  switch (os.platform()) {
    case 'darwin':
      return '.dylib';
    case 'win32':
      return '.dll';
    default:
      return '.so';
  }
}

function findLibrary(): string {
  const libName = 'libneam' + getLibraryExtension();

  // Check environment variable first
  const envPath = process.env.NEAM_LIBRARY_PATH;
  if (envPath && fs.existsSync(envPath)) {
    return envPath;
  }

  // Search paths in order of preference
  const searchPaths = [
    // Relative to this package (for development)
    path.join(__dirname, '..', '..', '..', '..', 'build', libName),
    path.join(__dirname, '..', '..', 'build', libName),
    // System locations
    path.join('/usr/local/lib', libName),
    path.join('/usr/lib', libName),
    // Homebrew on macOS
    path.join('/opt/homebrew/lib', libName),
    // Windows
    path.join('C:', 'Program Files', 'Neam', 'lib', libName),
  ];

  for (const searchPath of searchPaths) {
    if (fs.existsSync(searchPath)) {
      return searchPath;
    }
  }

  throw new Error(
    `Could not find ${libName}. Please ensure it is built and either:\n` +
      '1. Set NEAM_LIBRARY_PATH environment variable\n' +
      '2. Install it to a system library path\n' +
      "3. Build from source: cd build && cmake --build ."
  );
}

// Define opaque pointer types
const CNeamRuntime = koffi.opaque('NeamRuntime');
const CNeamBytecode = koffi.opaque('NeamBytecode');
const CNeamResult = koffi.opaque('NeamResult');
const CNeamValue = koffi.opaque('NeamValue');
const CNeamError = koffi.opaque('NeamError');
const CNeamAgent = koffi.opaque('NeamAgent');
const CNeamRunner = koffi.opaque('NeamRunner');
const CNeamTask = koffi.opaque('NeamTask');

// Pointer types
const NeamRuntimePtr = koffi.pointer(CNeamRuntime);
const NeamBytecodePtr = koffi.pointer(CNeamBytecode);
const NeamResultPtr = koffi.pointer(CNeamResult);
const NeamValuePtr = koffi.pointer(CNeamValue);
const NeamErrorPtr = koffi.pointer(CNeamError);
const NeamAgentPtr = koffi.pointer(CNeamAgent);
const NeamRunnerPtr = koffi.pointer(CNeamRunner);
const NeamTaskPtr = koffi.pointer(CNeamTask);

// Load the library
let lib: any = null;

function getLib(): any {
  if (lib) return lib;

  const libPath = findLibrary();
  lib = koffi.load(libPath);

  return lib;
}

// Define all functions lazily
let _funcs: any = null;

function getFuncs(): any {
  if (_funcs) return _funcs;

  const l = getLib();

  _funcs = {
    // Version
    neam_version: l.func('neam_version', 'str', []),
    neam_version_major: l.func('neam_version_major', 'int', []),
    neam_version_minor: l.func('neam_version_minor', 'int', []),
    neam_version_patch: l.func('neam_version_patch', 'int', []),

    // Runtime lifecycle
    neam_runtime_new: l.func('neam_runtime_new', NeamRuntimePtr, []),
    neam_runtime_new_with_config: l.func('neam_runtime_new_with_config', NeamRuntimePtr, ['str']),
    neam_runtime_free: l.func('neam_runtime_free', 'void', [NeamRuntimePtr]),
    neam_runtime_is_valid: l.func('neam_runtime_is_valid', 'int', [NeamRuntimePtr]),
    neam_runtime_reset: l.func('neam_runtime_reset', 'void', [NeamRuntimePtr]),

    // Compilation
    neam_compile: l.func('neam_compile', NeamBytecodePtr, [NeamRuntimePtr, 'str']),
    neam_compile_file: l.func('neam_compile_file', NeamBytecodePtr, [NeamRuntimePtr, 'str']),
    neam_bytecode_free: l.func('neam_bytecode_free', 'void', [NeamBytecodePtr]),
    neam_bytecode_to_bytes: l.func('neam_bytecode_to_bytes', 'uint8_t*', [NeamBytecodePtr, koffi.out(koffi.pointer('size_t'))]),
    neam_bytecode_from_bytes: l.func('neam_bytecode_from_bytes', NeamBytecodePtr, ['uint8_t*', 'size_t']),
    neam_bytes_free: l.func('neam_bytes_free', 'void', ['uint8_t*']),

    // Execution
    neam_run: l.func('neam_run', NeamResultPtr, [NeamRuntimePtr, 'str']),
    neam_run_file: l.func('neam_run_file', NeamResultPtr, [NeamRuntimePtr, 'str']),
    neam_run_bytecode: l.func('neam_run_bytecode', NeamResultPtr, [NeamRuntimePtr, NeamBytecodePtr]),
    neam_run_bytecode_bytes: l.func('neam_run_bytecode_bytes', NeamResultPtr, [NeamRuntimePtr, 'uint8_t*', 'size_t']),

    // Result
    neam_result_is_error: l.func('neam_result_is_error', 'int', [NeamResultPtr]),
    neam_result_output: l.func('neam_result_output', 'str', [NeamResultPtr]),
    neam_result_value: l.func('neam_result_value', NeamValuePtr, [NeamResultPtr]),
    neam_result_error: l.func('neam_result_error', NeamErrorPtr, [NeamResultPtr]),
    neam_result_free: l.func('neam_result_free', 'void', [NeamResultPtr]),

    // Value creation
    neam_value_nil: l.func('neam_value_nil', NeamValuePtr, []),
    neam_value_bool: l.func('neam_value_bool', NeamValuePtr, ['int']),
    neam_value_number: l.func('neam_value_number', NeamValuePtr, ['double']),
    neam_value_string: l.func('neam_value_string', NeamValuePtr, ['str', 'int']),
    neam_value_list_new: l.func('neam_value_list_new', NeamValuePtr, []),
    neam_value_list_append: l.func('neam_value_list_append', 'void', [NeamValuePtr, NeamValuePtr]),
    neam_value_map_new: l.func('neam_value_map_new', NeamValuePtr, []),
    neam_value_map_set: l.func('neam_value_map_set', 'void', [NeamValuePtr, 'str', NeamValuePtr]),

    // Value access
    neam_value_type: l.func('neam_value_type', 'int', [NeamValuePtr]),
    neam_value_as_bool: l.func('neam_value_as_bool', 'int', [NeamValuePtr]),
    neam_value_as_number: l.func('neam_value_as_number', 'double', [NeamValuePtr]),
    neam_value_as_string: l.func('neam_value_as_string', 'str', [NeamValuePtr]),
    neam_value_string_length: l.func('neam_value_string_length', 'size_t', [NeamValuePtr]),
    neam_value_list_length: l.func('neam_value_list_length', 'size_t', [NeamValuePtr]),
    neam_value_list_get: l.func('neam_value_list_get', NeamValuePtr, [NeamValuePtr, 'size_t']),
    neam_value_map_get: l.func('neam_value_map_get', NeamValuePtr, [NeamValuePtr, 'str']),
    neam_value_map_has: l.func('neam_value_map_has', 'int', [NeamValuePtr, 'str']),
    neam_value_to_json: l.func('neam_value_to_json', 'str', [NeamValuePtr]),
    neam_value_from_json: l.func('neam_value_from_json', NeamValuePtr, ['str']),
    neam_value_free: l.func('neam_value_free', 'void', [NeamValuePtr]),
    neam_string_free: l.func('neam_string_free', 'void', ['str']),

    // Globals
    neam_get_global: l.func('neam_get_global', NeamValuePtr, [NeamRuntimePtr, 'str']),
    neam_set_global: l.func('neam_set_global', 'int', [NeamRuntimePtr, 'str', NeamValuePtr]),

    // Tools
    neam_has_tool: l.func('neam_has_tool', 'int', [NeamRuntimePtr, 'str']),

    // Agent
    neam_agent_create: l.func('neam_agent_create', NeamAgentPtr, [NeamRuntimePtr, 'str', 'str', 'str', 'str']),
    neam_agent_set_temperature: l.func('neam_agent_set_temperature', 'void', [NeamAgentPtr, 'double']),
    neam_agent_ask: l.func('neam_agent_ask', 'str', [NeamAgentPtr, 'str']),
    neam_agent_reset: l.func('neam_agent_reset', 'void', [NeamAgentPtr]),
    neam_agent_free: l.func('neam_agent_free', 'void', [NeamAgentPtr]),

    // Runner
    neam_runner_create: l.func('neam_runner_create', NeamRunnerPtr, [NeamRuntimePtr, 'str', 'int']),
    neam_runner_enable_tracing: l.func('neam_runner_enable_tracing', 'void', [NeamRunnerPtr, 'int']),
    neam_runner_run: l.func('neam_runner_run', NeamResultPtr, [NeamRunnerPtr, 'str']),
    neam_runner_final_agent: l.func('neam_runner_final_agent', 'str', [NeamRunnerPtr]),
    neam_runner_total_turns: l.func('neam_runner_total_turns', 'int', [NeamRunnerPtr]),
    neam_runner_free: l.func('neam_runner_free', 'void', [NeamRunnerPtr]),

    // Task
    neam_task_create: l.func('neam_task_create', NeamTaskPtr, [NeamRuntimePtr, 'str', 'str']),
    neam_task_id: l.func('neam_task_id', 'str', [NeamTaskPtr]),
    neam_task_submit: l.func('neam_task_submit', 'int', [NeamTaskPtr]),
    neam_task_status: l.func('neam_task_status', 'int', [NeamTaskPtr]),
    neam_task_wait: l.func('neam_task_wait', 'int', [NeamTaskPtr, 'int']),
    neam_task_result: l.func('neam_task_result', 'str', [NeamTaskPtr]),
    neam_task_cancel: l.func('neam_task_cancel', 'int', [NeamTaskPtr]),
    neam_task_free: l.func('neam_task_free', 'void', [NeamTaskPtr]),

    // Agent Card
    neam_get_agent_card: l.func('neam_get_agent_card', 'str', [NeamRuntimePtr, 'str']),
    neam_register_agent_card: l.func('neam_register_agent_card', 'int', [NeamRuntimePtr, 'str', 'str']),

    // Error
    neam_get_last_error: l.func('neam_get_last_error', NeamErrorPtr, [NeamRuntimePtr]),
    neam_error_type: l.func('neam_error_type', 'int', [NeamErrorPtr]),
    neam_error_message: l.func('neam_error_message', 'str', [NeamErrorPtr]),
    neam_error_line: l.func('neam_error_line', 'int', [NeamErrorPtr]),
    neam_error_column: l.func('neam_error_column', 'int', [NeamErrorPtr]),
    neam_clear_error: l.func('neam_clear_error', 'void', [NeamRuntimePtr]),

    // GC
    neam_gc_pause: l.func('neam_gc_pause', 'void', [NeamRuntimePtr]),
    neam_gc_resume: l.func('neam_gc_resume', 'void', [NeamRuntimePtr]),
    neam_gc_collect: l.func('neam_gc_collect', 'void', [NeamRuntimePtr]),
  };

  return _funcs;
}

// ============================================================================
// Version Functions
// ============================================================================

/** Get the Neam runtime version string */
export function version(): string {
  return getFuncs().neam_version();
}

/** Get the major version number */
export function versionMajor(): number {
  return getFuncs().neam_version_major();
}

/** Get the minor version number */
export function versionMinor(): number {
  return getFuncs().neam_version_minor();
}

/** Get the patch version number */
export function versionPatch(): number {
  return getFuncs().neam_version_patch();
}

// ============================================================================
// Error Classes
// ============================================================================

/** Base class for Neam errors */
export class NeamError extends Error {
  constructor(
    message: string,
    public readonly errorType: ErrorType = ErrorType.RUNTIME,
    public readonly line: number = -1,
    public readonly column: number = -1,
    public readonly stackTrace?: string
  ) {
    super(message);
    this.name = 'NeamError';
  }
}

/** Parse error */
export class ParseError extends NeamError {
  constructor(message: string, line: number = -1, column: number = -1) {
    super(message, ErrorType.PARSE, line, column);
    this.name = 'ParseError';
  }
}

/** Compile error */
export class CompileError extends NeamError {
  constructor(message: string, line: number = -1, column: number = -1) {
    super(message, ErrorType.COMPILE, line, column);
    this.name = 'CompileError';
  }
}

/** Runtime error */
export class RuntimeError extends NeamError {
  constructor(message: string, stackTrace?: string) {
    super(message, ErrorType.RUNTIME, -1, -1, stackTrace);
    this.name = 'RuntimeError';
  }
}

// ============================================================================
// Value Class
// ============================================================================

/** Represents a Neam value */
export class Value {
  private handle: any;
  private freed = false;

  constructor(handle: any) {
    this.handle = handle;
  }

  /** Get the value type */
  get type(): ValueType {
    if (this.freed || !this.handle) return ValueType.NIL;
    return getFuncs().neam_value_type(this.handle);
  }

  /** Check if value is nil */
  get isNil(): boolean {
    return this.type === ValueType.NIL;
  }

  /** Check if value is a boolean */
  get isBool(): boolean {
    return this.type === ValueType.BOOL;
  }

  /** Check if value is a number */
  get isNumber(): boolean {
    return this.type === ValueType.NUMBER;
  }

  /** Check if value is a string */
  get isString(): boolean {
    return this.type === ValueType.STRING;
  }

  /** Check if value is a list */
  get isList(): boolean {
    return this.type === ValueType.LIST;
  }

  /** Check if value is a map */
  get isMap(): boolean {
    return this.type === ValueType.MAP;
  }

  /** Get as boolean */
  asBool(): boolean {
    return getFuncs().neam_value_as_bool(this.handle) !== 0;
  }

  /** Get as number */
  asNumber(): number {
    return getFuncs().neam_value_as_number(this.handle);
  }

  /** Get as string */
  asString(): string {
    return getFuncs().neam_value_as_string(this.handle) ?? '';
  }

  /** Convert to JavaScript value */
  toJS(): any {
    const funcs = getFuncs();
    switch (this.type) {
      case ValueType.NIL:
        return null;
      case ValueType.BOOL:
        return this.asBool();
      case ValueType.NUMBER:
        return this.asNumber();
      case ValueType.STRING:
        return this.asString();
      case ValueType.LIST:
      case ValueType.MAP:
      case ValueType.OBJECT:
        const json = funcs.neam_value_to_json(this.handle);
        if (json) {
          try {
            return JSON.parse(json);
          } catch {
            return null;
          }
        }
        return null;
      default:
        return null;
    }
  }

  /** Convert to JSON string */
  toJSON(): string {
    return getFuncs().neam_value_to_json(this.handle) ?? 'null';
  }

  /** Create from JavaScript value */
  static fromJS(obj: any): Value {
    const funcs = getFuncs();
    if (obj === null || obj === undefined) {
      return new Value(funcs.neam_value_nil());
    }
    if (typeof obj === 'boolean') {
      return new Value(funcs.neam_value_bool(obj ? 1 : 0));
    }
    if (typeof obj === 'number') {
      return new Value(funcs.neam_value_number(obj));
    }
    if (typeof obj === 'string') {
      return new Value(funcs.neam_value_string(obj, obj.length));
    }
    if (Array.isArray(obj)) {
      const handle = funcs.neam_value_list_new();
      for (const item of obj) {
        const itemValue = Value.fromJS(item);
        funcs.neam_value_list_append(handle, itemValue.handle);
        itemValue.freed = true; // Ownership transferred
      }
      return new Value(handle);
    }
    if (typeof obj === 'object') {
      const handle = funcs.neam_value_map_new();
      for (const [key, val] of Object.entries(obj)) {
        const valValue = Value.fromJS(val);
        funcs.neam_value_map_set(handle, String(key), valValue.handle);
        valValue.freed = true; // Ownership transferred
      }
      return new Value(handle);
    }
    // Try JSON serialization
    const json = JSON.stringify(obj);
    return new Value(funcs.neam_value_from_json(json));
  }

  /** Create from JSON string */
  static fromJSON(json: string): Value {
    const handle = getFuncs().neam_value_from_json(json);
    if (!handle) {
      throw new Error(`Invalid JSON: ${json}`);
    }
    return new Value(handle);
  }

  /** Free the value */
  free(): void {
    if (!this.freed && this.handle) {
      getFuncs().neam_value_free(this.handle);
      this.freed = true;
    }
  }

  /** @internal Get the handle for internal use */
  _getHandle(): any {
    return this.handle;
  }
}

// ============================================================================
// Result Class
// ============================================================================

/** Represents the result of executing Neam code */
export class Result {
  private handle: any;
  private freed = false;

  constructor(handle: any) {
    this.handle = handle;
  }

  /** Check if result represents an error */
  get isError(): boolean {
    return getFuncs().neam_result_is_error(this.handle) !== 0;
  }

  /** Get all output emitted during execution */
  get output(): string {
    return getFuncs().neam_result_output(this.handle) ?? '';
  }

  /** Get the return value */
  get value(): Value | null {
    const handle = getFuncs().neam_result_value(this.handle);
    if (!handle) return null;
    return new Value(handle);
  }

  /** Get error information */
  get error(): { type: ErrorType; message: string; line: number; column: number } | null {
    if (!this.isError) return null;
    const funcs = getFuncs();
    const errorHandle = funcs.neam_result_error(this.handle);
    if (!errorHandle) return null;
    return {
      type: funcs.neam_error_type(errorHandle),
      message: funcs.neam_error_message(errorHandle) ?? '',
      line: funcs.neam_error_line(errorHandle),
      column: funcs.neam_error_column(errorHandle),
    };
  }

  /** Throw if result is an error */
  throwIfError(): void {
    if (this.isError) {
      const err = this.error;
      if (err) {
        if (err.type === ErrorType.PARSE) {
          throw new ParseError(err.message, err.line, err.column);
        }
        if (err.type === ErrorType.COMPILE) {
          throw new CompileError(err.message, err.line, err.column);
        }
        throw new RuntimeError(err.message);
      }
      throw new NeamError('Unknown error');
    }
  }

  /** Free the result */
  free(): void {
    if (!this.freed && this.handle) {
      getFuncs().neam_result_free(this.handle);
      this.freed = true;
    }
  }
}

// ============================================================================
// Bytecode Class
// ============================================================================

/** Represents compiled Neam bytecode */
export class Bytecode {
  private handle: any;
  private freed = false;

  constructor(handle: any) {
    this.handle = handle;
  }

  /** Serialize to Buffer */
  toBuffer(): Buffer {
    const funcs = getFuncs();
    const sizeArr = [0];
    const data = funcs.neam_bytecode_to_bytes(this.handle, sizeArr);
    if (!data) throw new Error('Failed to serialize bytecode');
    const size = sizeArr[0];
    const result = Buffer.alloc(size);
    // Copy from native memory to Buffer
    for (let i = 0; i < size; i++) {
      result[i] = koffi.decode(data, 'uint8_t', i);
    }
    funcs.neam_bytes_free(data);
    return result;
  }

  /** Load from Buffer */
  static fromBuffer(data: Buffer): Bytecode {
    const funcs = getFuncs();
    const handle = funcs.neam_bytecode_from_bytes(data, data.length);
    if (!handle) {
      throw new Error('Invalid bytecode data');
    }
    return new Bytecode(handle);
  }

  /** Free the bytecode */
  free(): void {
    if (!this.freed && this.handle) {
      getFuncs().neam_bytecode_free(this.handle);
      this.freed = true;
    }
  }

  /** @internal Get the handle */
  _getHandle(): any {
    return this.handle;
  }
}

// ============================================================================
// Runtime Class
// ============================================================================

/** Neam Runtime - The core execution environment */
export class Runtime {
  private handle: any;

  /**
   * Create a new Neam runtime
   * @param config - Optional configuration
   */
  constructor(config?: RuntimeConfig) {
    const funcs = getFuncs();
    if (config) {
      const configJson = JSON.stringify({
        default_provider: config.defaultProvider,
        default_model: config.defaultModel,
        thread_safe: config.threadSafe,
        max_stack_depth: config.maxStackDepth,
        enable_file_io: config.enableFileIo,
        enable_network: config.enableNetwork,
        enable_shell: config.enableShell,
        working_directory: config.workingDirectory,
      });
      this.handle = funcs.neam_runtime_new_with_config(configJson);
    } else {
      this.handle = funcs.neam_runtime_new();
    }

    if (!this.handle) {
      throw new Error('Failed to create Neam runtime');
    }
  }

  /** Check if runtime is valid */
  get isValid(): boolean {
    return this.handle && getFuncs().neam_runtime_is_valid(this.handle) !== 0;
  }

  /** Reset runtime state */
  reset(): void {
    getFuncs().neam_runtime_reset(this.handle);
  }

  /** Close and free the runtime */
  close(): void {
    if (this.handle) {
      getFuncs().neam_runtime_free(this.handle);
      this.handle = null;
    }
  }

  /**
   * Run Neam source code
   * @param source - Neam source code
   * @returns Execution result
   */
  run(source: string): Result {
    const handle = getFuncs().neam_run(this.handle, source);
    return new Result(handle);
  }

  /**
   * Run a Neam source file
   * @param filePath - Path to source file
   * @returns Execution result
   */
  runFile(filePath: string): Result {
    const handle = getFuncs().neam_run_file(this.handle, filePath);
    return new Result(handle);
  }

  /**
   * Compile source code to bytecode
   * @param source - Neam source code
   * @returns Compiled bytecode
   */
  compile(source: string): Bytecode {
    const funcs = getFuncs();
    const handle = funcs.neam_compile(this.handle, source);
    if (!handle) {
      const err = funcs.neam_get_last_error(this.handle);
      if (err) {
        const msg = funcs.neam_error_message(err);
        throw new CompileError(msg ?? 'Compilation failed');
      }
      throw new CompileError('Compilation failed');
    }
    return new Bytecode(handle);
  }

  /**
   * Run compiled bytecode
   * @param bytecode - Compiled bytecode
   * @returns Execution result
   */
  runBytecode(bytecode: Bytecode): Result {
    const handle = getFuncs().neam_run_bytecode(this.handle, bytecode._getHandle());
    return new Result(handle);
  }

  /**
   * Get a global variable
   * @param name - Variable name
   * @returns Value or null if not found
   */
  getGlobal(name: string): Value | null {
    const handle = getFuncs().neam_get_global(this.handle, name);
    if (!handle) return null;
    return new Value(handle);
  }

  /**
   * Set a global variable
   * @param name - Variable name
   * @param value - Value to set
   */
  setGlobal(name: string, value: any): void {
    const v = Value.fromJS(value);
    const result = getFuncs().neam_set_global(this.handle, name, v._getHandle());
    if (result !== 0) {
      throw new Error(`Failed to set global '${name}'`);
    }
  }

  /**
   * Check if a tool is registered
   * @param name - Tool name
   */
  hasTool(name: string): boolean {
    return getFuncs().neam_has_tool(this.handle, name) !== 0;
  }

  /** Pause garbage collection */
  gcPause(): void {
    getFuncs().neam_gc_pause(this.handle);
  }

  /** Resume garbage collection */
  gcResume(): void {
    getFuncs().neam_gc_resume(this.handle);
  }

  /** Force garbage collection */
  gcCollect(): void {
    getFuncs().neam_gc_collect(this.handle);
  }

  /** @internal Get the handle */
  _getHandle(): any {
    return this.handle;
  }
}

// ============================================================================
// Agent Class
// ============================================================================

/** Represents a Neam AI agent */
export class Agent {
  private handle: any;
  private runtime: Runtime;
  private name: string;

  /**
   * Create a new agent
   * @param runtime - The Neam runtime
   * @param name - Agent name
   * @param options - Agent options
   */
  constructor(runtime: Runtime, name: string, options: AgentOptions = {}) {
    this.runtime = runtime;
    this.name = name;
    const funcs = getFuncs();

    this.handle = funcs.neam_agent_create(
      runtime._getHandle(),
      name,
      options.provider ?? 'ollama',
      options.model ?? 'llama3.2:3b',
      options.system ?? ''
    );

    if (!this.handle) {
      throw new Error(`Failed to create agent '${name}'`);
    }

    if (options.temperature !== undefined) {
      this.setTemperature(options.temperature);
    }
  }

  /** Get the agent name */
  getName(): string {
    return this.name;
  }

  /**
   * Set the temperature
   * @param temperature - Temperature value (0.0 to 2.0)
   */
  setTemperature(temperature: number): void {
    getFuncs().neam_agent_set_temperature(this.handle, temperature);
  }

  /**
   * Send a query to the agent
   * @param query - The question or request
   * @returns The agent's response
   */
  ask(query: string): string {
    const response = getFuncs().neam_agent_ask(this.handle, query);
    return response ?? '';
  }

  /** Reset conversation history */
  reset(): void {
    getFuncs().neam_agent_reset(this.handle);
  }

  /** Free the agent */
  free(): void {
    if (this.handle) {
      getFuncs().neam_agent_free(this.handle);
      this.handle = null;
    }
  }
}

// ============================================================================
// Runner Class
// ============================================================================

/** Executes an agent loop with handoffs */
export class Runner {
  private handle: any;
  private runtime: Runtime;

  /**
   * Create a new runner
   * @param runtime - The Neam runtime
   * @param entryAgent - Entry agent name
   * @param maxTurns - Maximum turns
   */
  constructor(runtime: Runtime, entryAgent: string, maxTurns: number = 10) {
    this.runtime = runtime;
    const funcs = getFuncs();
    this.handle = funcs.neam_runner_create(runtime._getHandle(), entryAgent, maxTurns);

    if (!this.handle) {
      throw new Error('Failed to create runner');
    }
  }

  /**
   * Enable or disable tracing
   * @param enabled - Whether to enable tracing
   */
  enableTracing(enabled: boolean = true): void {
    getFuncs().neam_runner_enable_tracing(this.handle, enabled ? 1 : 0);
  }

  /**
   * Run the agent loop
   * @param input - Initial input
   * @returns Execution result
   */
  run(input: string): Result {
    const handle = getFuncs().neam_runner_run(this.handle, input);
    return new Result(handle);
  }

  /** Get the final agent name */
  get finalAgent(): string {
    return getFuncs().neam_runner_final_agent(this.handle) ?? '';
  }

  /** Get total turns used */
  get totalTurns(): number {
    return getFuncs().neam_runner_total_turns(this.handle);
  }

  /** Free the runner */
  free(): void {
    if (this.handle) {
      getFuncs().neam_runner_free(this.handle);
      this.handle = null;
    }
  }
}

// ============================================================================
// Task Class
// ============================================================================

/** Represents an A2A Protocol task */
export class Task {
  private handle: any;
  private runtime: Runtime;

  /**
   * Create a new task
   * @param runtime - The Neam runtime
   * @param agentName - Target agent name
   * @param input - Input data
   */
  constructor(runtime: Runtime, agentName: string, input: Record<string, any>) {
    this.runtime = runtime;
    const inputJson = JSON.stringify(input);
    const funcs = getFuncs();
    this.handle = funcs.neam_task_create(runtime._getHandle(), agentName, inputJson);

    if (!this.handle) {
      throw new Error('Failed to create task');
    }
  }

  /** Get the task ID */
  get id(): string {
    return getFuncs().neam_task_id(this.handle) ?? '';
  }

  /** Get the current status */
  get status(): TaskStatus {
    return getFuncs().neam_task_status(this.handle);
  }

  /** Get status as string */
  get statusName(): string {
    const names: Record<TaskStatus, string> = {
      [TaskStatus.PENDING]: 'pending',
      [TaskStatus.RUNNING]: 'running',
      [TaskStatus.COMPLETED]: 'completed',
      [TaskStatus.FAILED]: 'failed',
      [TaskStatus.CANCELLED]: 'cancelled',
    };
    return names[this.status] ?? 'unknown';
  }

  /** Submit the task */
  submit(): void {
    const result = getFuncs().neam_task_submit(this.handle);
    if (result !== 0) {
      throw new Error('Failed to submit task');
    }
  }

  /**
   * Wait for completion
   * @param timeoutMs - Timeout in milliseconds (-1 for infinite)
   * @returns True if completed
   */
  wait(timeoutMs: number = -1): boolean {
    return getFuncs().neam_task_wait(this.handle, timeoutMs) === 0;
  }

  /** Get the result */
  get result(): Record<string, any> | null {
    const json = getFuncs().neam_task_result(this.handle);
    if (json) {
      try {
        return JSON.parse(json);
      } catch {
        return null;
      }
    }
    return null;
  }

  /** Cancel the task */
  cancel(): void {
    const result = getFuncs().neam_task_cancel(this.handle);
    if (result !== 0) {
      throw new Error('Failed to cancel task');
    }
  }

  /** Free the task */
  free(): void {
    if (this.handle) {
      getFuncs().neam_task_free(this.handle);
      this.handle = null;
    }
  }
}

// ============================================================================
// Exports
// ============================================================================

export default {
  Runtime,
  Result,
  Value,
  Bytecode,
  Agent,
  Runner,
  Task,
  ValueType,
  ErrorType,
  TaskStatus,
  NeamError,
  ParseError,
  CompileError,
  RuntimeError,
  version,
  versionMajor,
  versionMinor,
  versionPatch,
};
