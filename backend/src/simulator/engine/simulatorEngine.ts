import { CommandRunner } from "./commandRunner";
import { CatalogService, SimctlClient } from "./simctlClient";
import { XcodebuildClient } from "./xcodebuildClient";

/** The host-facing side of the simulator backend: simctl, xcodebuild and the runtime catalog. */
export class SimulatorEngine {
  readonly simctl: SimctlClient;
  readonly xcodebuild: XcodebuildClient;
  readonly catalog: CatalogService;

  constructor(
    readonly runner: CommandRunner,
    workspaceRoot: string
  ) {
    this.simctl = new SimctlClient(runner);
    this.xcodebuild = new XcodebuildClient(runner, workspaceRoot);
    this.catalog = new CatalogService(this.simctl);
  }
}
