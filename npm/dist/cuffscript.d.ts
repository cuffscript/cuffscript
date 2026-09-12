export interface RunOutcome {
  success: boolean;
  error: string;
}

export interface CuffScriptFS {
  mkdirTree(path: string): void;
  writeFile(path: string, data: string | Uint8Array): void;
  readdir(path: string): string[];
  stat(path: string): { mode: number };
  isDir(mode: number): boolean;
  unlink(path: string): void;
  rmdir(path: string): void;
  analyzePath(path: string): { exists: boolean };
}

export interface CuffScriptModule {
  // Parses and executes CuffScript source. `scriptDir` is the virtual-FS
  // directory used to resolve `use <name> from <path>` imports.
  cuffRun(source: string, scriptDir: string): RunOutcome;
  // Parses `source` only and writes a token/AST dump through `print`.
  cuffDump(source: string): RunOutcome;
  // Emscripten's virtual filesystem (MEMFS). Available because the module
  // is built with FORCE_FILESYSTEM=1 — use it to mount multi-file projects
  // before calling cuffRun/cuffDump.
  FS: CuffScriptFS;
}

export interface CuffScriptModuleConfig {
  // Called once per line written to stdout (i.e. once per print()).
  print?: (text: string) => void;
  // Called once per line written to stderr.
  printErr?: (text: string) => void;
  // Called once per byte read from stdin (i.e. by input()). Return the next
  // byte (0-255), or null/undefined at end of input.
  stdin?: () => number | null | undefined;
  // Override where the .wasm binary is fetched from, e.g. for a custom CDN.
  locateFile?: (path: string, scriptDirectory: string) => string;
}

export default function createCuffScriptModule(
  config?: CuffScriptModuleConfig,
): Promise<CuffScriptModule>;
