#!/usr/bin/env node
// Host-side runner for the browser logic suite.
//   node web/test/run-node.js
const { runTests } = require('../tests.js');

const results = runTests();
let failed = 0;

for (const r of results) {
  if (r.pass) {
    console.log('PASS  ' + r.name);
  } else {
    ++failed;
    console.log('FAIL  ' + r.name + '\n      ' + r.error);
  }
}

console.log('\n' + results.length + ' scenarios: ' +
            (results.length - failed) + ' passed, ' + failed + ' failed');
console.log(failed === 0 ? 'RESULT OK' : 'RESULT FAIL');
process.exit(failed === 0 ? 0 : 1);
