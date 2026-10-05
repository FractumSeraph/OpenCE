import { execFileSync } from "node:child_process";
import { readFileSync, statSync } from "node:fs";

const allowedBinaryAssets = new Set([
  "port/android/app/src/main/res/mipmap-hdpi/ic_launcher_foreground.png",
  "port/android/app/src/main/res/mipmap-mdpi/ic_launcher_foreground.png",
  "port/android/app/src/main/res/mipmap-xhdpi/ic_launcher_foreground.png",
  "port/android/app/src/main/res/mipmap-xxhdpi/ic_launcher_foreground.png",
  "port/android/app/src/main/res/mipmap-xxxhdpi/ic_launcher_foreground.png",
  "port/android/art/android-icon.png",
  "port/android/gradle/wrapper/gradle-wrapper.jar",
]);
const assetExtension = /\.(?:a|apk|bin|data|dll|dylib|exe|gif|iso|jar|jpe?g|map|mp3|o|obj|ogg|pak|png|profdata|so|wasm|wav|webp|xiso|zip)$/iu;
const prohibitedProductName = ["spell", "book"].join("");
const paths = execFileSync("git", ["ls-files", "-z"], { encoding: "utf8" }).split("\0").filter(Boolean);
const problems = [];

for (const path of paths) {
  const lower = path.toLowerCase();
  if (/(^|\/)(?:assets|build|dist|node_modules|original|research|\.wrangler)(?:\/|$)/u.test(lower)) {
    problems.push(`${path}: generated, proprietary, or research material is not permitted`);
    continue;
  }
  if (/(^|\/)(?:\.env(?:\..*)?|wrangler\.toml\.secret|secrets?\.json)$/u.test(lower)) {
    problems.push(`${path}: secret-bearing file is not permitted`);
    continue;
  }
  if (lower.includes(prohibitedProductName)) problems.push(`${path}: prohibited material name`);
  if (assetExtension.test(path) && !allowedBinaryAssets.has(path)) {
    problems.push(`${path}: game/runtime assets are not permitted in Git history`);
    continue;
  }
  if (statSync(path).size > 15_000_000) {
    problems.push(`${path}: unexpectedly large tracked file`);
    continue;
  }
  if (statSync(path).size > 2_000_000) continue;
  const bytes = readFileSync(path);
  if (!bytes.includes(0) && bytes.toString("utf8").toLowerCase().includes(prohibitedProductName)) {
    problems.push(`${path}: prohibited product material`);
  }
}

if (problems.length) {
  console.error(problems.join("\n"));
  process.exit(1);
}
console.log(`Verified ${paths.length} tracked source files.`);
