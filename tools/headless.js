/* Headless loader for the testbench: extracts the <script> from
   ui/testbench.html, stubs the DOM, and returns the live `window`
   (bench API included). The single source of truth stays
   ui/testbench.html — this file contains NO brain or scenario logic.

   CAVEAT (finding 32): NOT bit-identical to the browser yet. Chrome
   >=148 computes Math.tanh via the host libm while Node 24's V8 13.6
   uses the fdlibm port — measured 2 ulp apart at tanh(0.8) on the same
   machine. Board numbers agree to ~0.3 pct (car is the most sensitive).
   Official numbers come from the browser rig until the software-tanh
   determinism batch lands; then this runner becomes the official CI. */
'use strict';
const fs = require('fs');
const path = require('path');

function makeElement() {
  const el = {
    style: {}, dataset: {},
    classList: { add() {}, remove() {}, toggle() {} },
    textContent: '', innerHTML: '', value: '0',
    childNodes: { length: 0 }, children: [],
    scrollTop: 0, scrollHeight: 0,
    clientWidth: 800, clientHeight: 600, width: 800, height: 600,
    appendChild() {}, removeChild() {}, addEventListener() {},
    setAttribute() {}, removeAttribute() {}, click() {},
    getBoundingClientRect: () => ({ left: 0, top: 0 }),
    querySelector: () => makeElement(),
    querySelectorAll: () => [],
    getContext: () => new Proxy({}, { get: () => () => {} }),
  };
  return el;
}

function loadBench(htmlPath) {
  const file = htmlPath ||
    path.join(__dirname, '..', 'ui', 'testbench.html');
  const html = fs.readFileSync(file, 'utf8');
  const a = html.indexOf('<script>') + '<script>'.length;
  const b = html.lastIndexOf('</script>');
  if (a < 8 || b < 0) throw new Error('no <script> block found in ' + file);
  const src = html.slice(a, b);

  const document = {
    getElementById: () => makeElement(),
    createElement: () => makeElement(),
    addEventListener() {},
    querySelectorAll: () => [],
  };
  const window = { BENCHSEED: 0 };
  const fn = new Function(
    'window', 'document', 'requestAnimationFrame', 'devicePixelRatio', src);
  fn(window, document, () => {}, 1);
  if (!window.bench) throw new Error('bench API did not initialize');
  return window;
}

module.exports = { loadBench };
