import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
test('source-only pushes and pull requests keep source checks and skip component compilation',()=>{
  const files=fs.readdirSync(path.join(root,'.github/workflows')).filter(x=>x.endsWith('.yml'));
  const text=files.map(x=>fs.readFileSync(path.join(root,'.github/workflows',x),'utf8')).join('\n');
  assert.match(text,/source:\n    runs-on:/);
  assert.ok(text.includes("node --test scripts/test-policy.test.mjs scripts/test-store-context.test.mjs scripts/test-source-only.test.mjs"));
  assert.ok(text.includes(`component:\n    if: "!contains(github.event.head_commit.message, '[source-only]') && !contains(github.event.pull_request.title, '[source-only]')"`));
  for(const message of ['ordinary source','[source-only] source integration'])
    for(const title of ['', 'ordinary pull request','[source-only] integrate source'])
      assert.equal(!message.includes('[source-only]')&&!title.includes('[source-only]'),!message.startsWith('[source-only]')&&!title.startsWith('[source-only]'));
});
