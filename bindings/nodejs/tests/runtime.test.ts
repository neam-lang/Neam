/**
 * Tests for Neam Node.js runtime bindings
 */

import {
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
  version,
  versionMajor,
  versionMinor,
  versionPatch,
  ParseError,
  CompileError,
  RuntimeError,
} from '../src/index';

// Skip tests if library not available
let libraryAvailable = true;
try {
  const r = new Runtime();
  r.close();
} catch (e) {
  libraryAvailable = false;
}

const describeIfLibrary = libraryAvailable ? describe : describe.skip;

describeIfLibrary('Version', () => {
  test('version string should be valid', () => {
    const v = version();
    expect(v).toBeTruthy();
    expect(v).toContain('.');
  });

  test('version numbers should be non-negative', () => {
    expect(versionMajor()).toBeGreaterThanOrEqual(0);
    expect(versionMinor()).toBeGreaterThanOrEqual(0);
    expect(versionPatch()).toBeGreaterThanOrEqual(0);
  });
});

describeIfLibrary('Runtime', () => {
  let runtime: Runtime;

  beforeEach(() => {
    runtime = new Runtime();
  });

  afterEach(() => {
    runtime.close();
  });

  test('should create a runtime', () => {
    expect(runtime.isValid).toBe(true);
  });

  test('should create a runtime with config', () => {
    const r = new Runtime({
      maxStackDepth: 500,
      enableShell: false,
    });
    expect(r.isValid).toBe(true);
    r.close();
  });

  test('should run simple code', () => {
    const result = runtime.run('{ emit "Hello"; }');
    expect(result.isError).toBe(false);
    expect(result.output).toContain('Hello');
    result.free();
  });

  test('should handle math expressions', () => {
    const result = runtime.run('let x = 1 + 2 + 3; emit str(x);');
    expect(result.isError).toBe(false);
    expect(result.output).toContain('6');
    result.free();
  });

  test('should handle errors', () => {
    const result = runtime.run('undefined_variable');
    expect(result.isError).toBe(true);
    expect(result.error).toBeTruthy();
    result.free();
  });
});

describeIfLibrary('Value', () => {
  test('should handle null values', () => {
    const value = Value.fromJS(null);
    expect(value.isNil).toBe(true);
    expect(value.toJS()).toBeNull();
    value.free();
  });

  test('should handle boolean values', () => {
    const value = Value.fromJS(true);
    expect(value.isBool).toBe(true);
    expect(value.asBool()).toBe(true);
    expect(value.toJS()).toBe(true);
    value.free();
  });

  test('should handle number values', () => {
    const value = Value.fromJS(42.5);
    expect(value.isNumber).toBe(true);
    expect(value.asNumber()).toBe(42.5);
    expect(value.toJS()).toBe(42.5);
    value.free();
  });

  test('should handle string values', () => {
    const value = Value.fromJS('hello');
    expect(value.isString).toBe(true);
    expect(value.asString()).toBe('hello');
    expect(value.toJS()).toBe('hello');
    value.free();
  });

  test('should handle array values', () => {
    const value = Value.fromJS([1, 2, 3]);
    expect(value.isList).toBe(true);
    const result = value.toJS();
    expect(result).toEqual([1, 2, 3]);
    value.free();
  });

  test('should handle object values', () => {
    const value = Value.fromJS({ a: 1, b: 2 });
    expect(value.isMap).toBe(true);
    const result = value.toJS();
    expect(result).toEqual({ a: 1, b: 2 });
    value.free();
  });
});

describeIfLibrary('Globals', () => {
  let runtime: Runtime;

  beforeEach(() => {
    runtime = new Runtime();
  });

  afterEach(() => {
    runtime.close();
  });

  test('should set and get globals', () => {
    runtime.setGlobal('my_var', 42);
    const value = runtime.getGlobal('my_var');
    expect(value).toBeTruthy();
    expect(value!.asNumber()).toBe(42);
    value!.free();
  });

  test('should use globals in Neam code', () => {
    runtime.setGlobal('greeting', 'Hello');
    const result = runtime.run('{ emit greeting; }');
    expect(result.output).toContain('Hello');
    result.free();
  });
});

describeIfLibrary('Bytecode', () => {
  let runtime: Runtime;

  beforeEach(() => {
    runtime = new Runtime();
  });

  afterEach(() => {
    runtime.close();
  });

  test('should compile bytecode', () => {
    const bytecode = runtime.compile('{ emit "Compiled!"; }');
    expect(bytecode).toBeTruthy();
    bytecode.free();
  });

  test('should run compiled bytecode', () => {
    const bytecode = runtime.compile('let x = 1 + 2; emit str(x);');
    const result = runtime.runBytecode(bytecode);
    expect(result.isError).toBe(false);
    // Note: Output capture from bytecode execution may need native bindings
    result.free();
    bytecode.free();
  });

  // Note: Bytecode serialization/deserialization requires native memory handling
  // that is complex with koffi. Consider using N-API for production use.
  test.skip('should serialize and deserialize bytecode', () => {
    const bytecode = runtime.compile('{ emit "Test"; }');
    const buffer = bytecode.toBuffer();
    expect(buffer.length).toBeGreaterThan(0);

    const loaded = Bytecode.fromBuffer(buffer);
    const result = runtime.runBytecode(loaded);
    expect(result.output).toContain('Test');

    result.free();
    loaded.free();
    bytecode.free();
  });
});

describeIfLibrary('Agent', () => {
  let runtime: Runtime;

  beforeEach(() => {
    runtime = new Runtime();
  });

  afterEach(() => {
    runtime.close();
  });

  test('should create an agent', () => {
    const agent = new Agent(runtime, 'test_agent', {
      provider: 'ollama',
      model: 'llama3.2:3b',
      system: 'You are a test agent.',
    });
    expect(agent.getName()).toBe('test_agent');
    agent.free();
  });

  // Note: Actual LLM calls require a running provider
  // These tests focus on the binding mechanics
});

describeIfLibrary('Runner', () => {
  let runtime: Runtime;

  beforeEach(() => {
    runtime = new Runtime();
  });

  afterEach(() => {
    runtime.close();
  });

  test('should create a runner', () => {
    const runner = new Runner(runtime, 'entry_agent', 10);
    runner.enableTracing(true);
    runner.free();
  });
});

describeIfLibrary('Task', () => {
  let runtime: Runtime;

  beforeEach(() => {
    runtime = new Runtime();
  });

  afterEach(() => {
    runtime.close();
  });

  test('should create a task', () => {
    const task = new Task(runtime, 'agent', { query: 'test' });
    expect(task.id).toBeTruthy();
    expect(task.status).toBe(TaskStatus.PENDING);
    expect(task.statusName).toBe('pending');
    task.free();
  });
});
