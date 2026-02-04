/**
 * High-level JavaScript wrapper for Neam WebAssembly module.
 *
 * Usage:
 *   import { Neam } from './neam-wrapper.js';
 *
 *   const neam = await Neam.create();
 *   const output = neam.run('emit "Hello, World!"');
 *   console.log(output);
 *   neam.shutdown();
 */

/**
 * Neam - High-level wrapper for the Neam WebAssembly module
 */
export class Neam {
  /**
   * @private
   * @type {any}
   */
  _module = null;

  /**
   * @private
   * @type {boolean}
   */
  _initialized = false;

  /**
   * Create a new Neam instance.
   * Use Neam.create() instead of calling this directly.
   * @param {any} module - The loaded WASM module
   */
  constructor(module) {
    this._module = module;
  }

  /**
   * Create and initialize a new Neam instance.
   * @returns {Promise<Neam>} Initialized Neam instance
   */
  static async create() {
    // Import the Emscripten-generated module
    const NeamModule = (await import('./neam.js')).default;
    const module = await NeamModule();

    const neam = new Neam(module);
    const result = module._neam_init();
    if (result !== 0) {
      throw new Error('Failed to initialize Neam runtime');
    }
    neam._initialized = true;
    return neam;
  }

  /**
   * Run Neam source code.
   * @param {string} source - Neam source code
   * @returns {string} Output from the program
   * @throws {Error} If execution fails
   */
  run(source) {
    this._checkInitialized();

    // Allocate string in WASM memory
    const sourceBytes = this._module.lengthBytesUTF8(source) + 1;
    const sourcePtr = this._module._malloc(sourceBytes);
    this._module.stringToUTF8(source, sourcePtr, sourceBytes);

    try {
      const result = this._module._neam_run(sourcePtr);
      if (result === 0) {
        const error = this._module.UTF8ToString(this._module._neam_get_error());
        throw new Error(error || 'Execution failed');
      }
      return this._module.UTF8ToString(this._module._neam_get_output());
    } finally {
      this._module._free(sourcePtr);
    }
  }

  /**
   * Compile Neam source code to bytecode.
   * @param {string} source - Neam source code
   * @returns {Uint8Array} Compiled bytecode
   * @throws {Error} If compilation fails
   */
  compile(source) {
    this._checkInitialized();

    // Allocate string in WASM memory
    const sourceBytes = this._module.lengthBytesUTF8(source) + 1;
    const sourcePtr = this._module._malloc(sourceBytes);
    this._module.stringToUTF8(source, sourcePtr, sourceBytes);

    // Allocate size output
    const sizePtr = this._module._malloc(4);

    try {
      const bytecodePtr = this._module._neam_compile(sourcePtr, sizePtr);
      if (bytecodePtr === 0) {
        const error = this._module.UTF8ToString(this._module._neam_get_error());
        throw new Error(error || 'Compilation failed');
      }

      // Read size and copy bytecode
      const size = this._module.HEAPU32[sizePtr >> 2];
      const bytecode = new Uint8Array(size);
      bytecode.set(new Uint8Array(this._module.HEAPU8.buffer, bytecodePtr, size));

      this._module._free(bytecodePtr);
      return bytecode;
    } finally {
      this._module._free(sourcePtr);
      this._module._free(sizePtr);
    }
  }

  /**
   * Run compiled bytecode.
   * @param {Uint8Array} bytecode - Compiled bytecode
   * @returns {string} Output from the program
   * @throws {Error} If execution fails
   */
  runBytecode(bytecode) {
    this._checkInitialized();

    // Allocate bytecode in WASM memory
    const bytecodePtr = this._module._malloc(bytecode.length);
    this._module.HEAPU8.set(bytecode, bytecodePtr);

    try {
      const result = this._module._neam_run_bytecode(bytecodePtr, bytecode.length);
      if (result === 0) {
        const error = this._module.UTF8ToString(this._module._neam_get_error());
        throw new Error(error || 'Execution failed');
      }
      return this._module.UTF8ToString(this._module._neam_get_output());
    } finally {
      this._module._free(bytecodePtr);
    }
  }

  /**
   * Get the last error message.
   * @returns {string} Error message
   */
  getError() {
    if (!this._module) return '';
    return this._module.UTF8ToString(this._module._neam_get_error());
  }

  /**
   * Shutdown and release resources.
   */
  shutdown() {
    if (this._module && this._initialized) {
      this._module._neam_shutdown();
      this._initialized = false;
    }
  }

  /**
   * @private
   */
  _checkInitialized() {
    if (!this._initialized) {
      throw new Error('Neam runtime not initialized');
    }
  }
}

/**
 * Value type enumeration
 */
export const ValueType = {
  NIL: 0,
  BOOL: 1,
  NUMBER: 2,
  STRING: 3,
  LIST: 4,
  MAP: 5,
  FUNCTION: 6,
  AGENT: 7,
  OBJECT: 8,
};

export default Neam;
