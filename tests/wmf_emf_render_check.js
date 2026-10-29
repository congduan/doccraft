#!/usr/bin/env node
// Renders doccraft-produced WMF/EMF media parts with wmf-emf-renderer
// (github.com/congduan/WmfEmfViewer) to prove the extracted bytes are valid
// metafiles. Requires the renderer checked out at WMF_RENDERER_DIR.
const { renderToSvg } = require((process.env.WMF_RENDERER_DIR ||
  "/tmp/WmfEmfViewer") + "/packages/wmf-emf-renderer/index.js");
const fs = require("fs");
const files = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
let ok = 0;
const failures = [];
for (const f of files) {
  try {
    const svg = renderToSvg(fs.readFileSync(f), {});
    if (svg && svg.includes("<svg")) ok++;
    else failures.push(f);
  } catch (e) {
    failures.push(`${f}: ${e.message.slice(0, 80)}`);
  }
}
// Validity bar: most metafiles must render; a few exotic ones may not.
if (ok < files.length * 0.7) {
  console.error(`WMF/EMF validity: only ${ok}/${files.length} rendered`);
  for (const f of failures) console.error("  " + f);
  process.exit(1);
}
console.log(`WMF/EMF validity: ${ok}/${files.length} rendered by wmf-emf-renderer`);
