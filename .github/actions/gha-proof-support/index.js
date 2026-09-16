const {spawnSync} = require("child_process");

const result = spawnSync("bash", ["scripts/ci/ci.sh", "gha-remote-storage"], {
  stdio: "inherit",
});

if (result.error) {
  throw result.error;
}
process.exitCode = result.status === null ? 1 : result.status;
