import * as std from "std";

if (scriptArgs.length !== 3) throw new Error("usage: runner.js ADAPTER RAW_JSON");
const adapter = scriptArgs[1];
if (!/^[a-z0-9-]+$/.test(adapter)) throw new Error("invalid adapter name");
const source = std.loadFile(`/usr/lib/microfx/data-adapters/${adapter}.js`);
if (source === null) throw new Error("adapter does not exist");
eval(source);
const raw = std.loadFile(scriptArgs[2]);
if (raw === null) throw new Error("raw input does not exist");
const result = normalize(JSON.parse(raw));
std.out.puts(`${JSON.stringify(result)}\n`);
std.out.flush();
