// Заглушки браузера, чтобы proto/maze.js исполнялся в jsc без DOM.
globalThis.window = globalThis;
globalThis.document = { getElementById: () => null, querySelectorAll: () => [] };
window.addEventListener = () => {};
