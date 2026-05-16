// Run from the project root:  node tests/test_hljs.js highlight.min.js
// Exits 0 if hljs highlights Python hello-world with the expected span classes,
// exits 1 with a descriptive message if anything is missing.

const fs   = require('fs');
const path = require('path');

const hljsPath = process.argv[2];
if (!hljsPath) {
    console.error('Usage: node test_hljs.js <path-to-highlight.min.js>');
    process.exit(1);
}

// Load hljs into this Node context
const src = fs.readFileSync(hljsPath, 'utf8');
const module_ = { exports: {} };
// hljs ships as a UMD bundle; evaluate it so hljs lands on module_.exports
(new Function('module', 'exports', src))(module_, module_.exports);
const hljs = module_.exports;

// Python hello-world — exercises keyword, built-in, string, punctuation
const code = 'print("Hello, world!")';
const result = hljs.highlight(code, { language: 'python' });

let failures = 0;

function check(label, needle) {
    if (!result.value.includes(needle)) {
        console.error(`FAIL [${label}]: expected "${needle}" in highlighted output`);
        console.error('  got: ' + result.value);
        failures++;
    } else {
        console.log(`PASS [${label}]`);
    }
}

// print is a built-in — hljs marks it hljs-built_in
check('hljs-builtin',  'hljs-built_in');
// "Hello, world!" is a string literal
check('hljs-string',   'hljs-string');

process.exit(failures > 0 ? 1 : 0);
