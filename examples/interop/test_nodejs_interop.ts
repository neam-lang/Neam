/**
 * Neam Node.js Interoperability Test
 *
 * This script tests the Neam-Node.js interoperability via the C API bindings.
 *
 * Requirements:
 *   - Build libneam: cd build && cmake --build .
 *   - Set library path: export NEAM_LIBRARY_PATH=/path/to/libneam.dylib
 *   - Install dependencies: cd bindings/nodejs && npm install
 *
 * Usage:
 *   npx ts-node test_nodejs_interop.ts
 *   OR
 *   node test_nodejs_interop.js (after compiling)
 */

import {
  Runtime,
  Value,
  version,
  versionMajor,
  versionMinor,
  versionPatch,
  ValueType,
} from '../../bindings/nodejs/src/index';

async function testInteroperability(): Promise<boolean> {
  console.log('='.repeat(60));
  console.log('Neam Node.js Interoperability Test');
  console.log('='.repeat(60));

  // Test 1: Version Check
  console.log('\n[1/7] Checking Neam version...');
  try {
    console.log(`  Version: ${version()}`);
    console.log(`  Major: ${versionMajor()}, Minor: ${versionMinor()}, Patch: ${versionPatch()}`);
  } catch (e) {
    console.log(`  [ERROR] Failed to get version: ${e}`);
    return false;
  }

  // Test 2: Create Runtime
  console.log('\n[2/7] Creating Neam Runtime...');
  let runtime: Runtime;
  try {
    runtime = new Runtime();
    console.log(`  Runtime valid: ${runtime.isValid}`);
  } catch (e) {
    console.log(`  [ERROR] Failed to create runtime: ${e}`);
    return false;
  }

  // Test 3: Execute Simple Neam Code
  console.log('\n[3/7] Executing Neam code from Node.js...');
  try {
    const result = runtime.run('{ emit "Hello from Neam (via Node.js)!"; }');
    console.log(`  Output: ${result.output.trim()}`);
    console.log(`  Is Error: ${result.isError}`);
    result.free();
  } catch (e) {
    console.log(`  [ERROR] Execution failed: ${e}`);
    return false;
  }

  // Test 4: Set Global Variable from Node.js
  console.log('\n[4/7] Setting global variable from Node.js...');
  try {
    runtime.setGlobal('nodejs_message', 'Hello from Node.js!');
    runtime.setGlobal('nodejs_number', 99.9);
    runtime.setGlobal('nodejs_array', [1, 2, 3]);
    console.log("  Set: nodejs_message = 'Hello from Node.js!'");
    console.log('  Set: nodejs_number = 99.9');
    console.log('  Set: nodejs_array = [1, 2, 3]');
  } catch (e) {
    console.log(`  [ERROR] Failed to set global: ${e}`);
    return false;
  }

  // Test 5: Get Global Variable from Neam
  console.log('\n[5/7] Getting global variable in Neam...');
  try {
    const result = runtime.run('{ emit nodejs_message; emit str(nodejs_number); }');
    console.log(`  Output: ${result.output}`);
    result.free();
  } catch (e) {
    console.log(`  [ERROR] Failed to get global: ${e}`);
    return false;
  }

  // Test 6: Value Conversion JS <-> Neam
  console.log('\n[6/7] Testing value conversions...');
  try {
    const original = {
      name: 'Neam',
      version: '0.6.0',
      features: ['agents', 'RAG', 'voice'],
      enabled: true,
    };

    const value = Value.fromJS(original);
    console.log(`  Original JS object: ${JSON.stringify(original)}`);
    console.log(`  As Neam Value JSON: ${value.toJSON()}`);

    const converted = value.toJS();
    console.log(`  Back to JS: ${JSON.stringify(converted)}`);

    // Check round-trip
    if (original.name === converted.name) {
      console.log('  Round-trip: SUCCESS');
    } else {
      console.log('  Round-trip: MISMATCH');
    }
    value.free();
  } catch (e) {
    console.log(`  [ERROR] Value conversion failed: ${e}`);
    return false;
  }

  // Test 7: Compile and Run Bytecode
  console.log('\n[7/7] Testing bytecode compilation...');
  try {
    const bytecode = runtime.compile('{ emit "Bytecode execution from Node.js"; }');
    const buffer = bytecode.toBuffer();
    console.log(`  Compiled to ${buffer.length} bytes`);

    // Execute bytecode
    const result = runtime.runBytecode(bytecode);
    console.log(`  Bytecode output: ${result.output.trim()}`);
    result.free();
    bytecode.free();
  } catch (e) {
    console.log(`  [ERROR] Bytecode test failed: ${e}`);
    return false;
  }

  // Cleanup
  runtime.close();

  console.log('\n' + '='.repeat(60));
  console.log('All interoperability tests PASSED!');
  console.log('='.repeat(60));
  return true;
}

// Run tests
testInteroperability()
  .then((success) => {
    process.exit(success ? 0 : 1);
  })
  .catch((e) => {
    console.error('Test failed with exception:', e);
    process.exit(1);
  });
