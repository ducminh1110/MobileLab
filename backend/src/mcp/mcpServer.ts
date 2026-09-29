import { z } from "zod";
import { readVersion } from "../config/version";
import { OrchestratorService } from "../orchestrator/services/orchestratorService";
import { errorMessage } from "../utils/errors";

export interface McpRequest {
  jsonrpc?: string;
  /** Absent for notifications, which get no response. */
  id?: string | number | null;
  method: string;
  params?: unknown;
}

export interface McpResponse {
  jsonrpc: "2.0";
  id: string | number | null;
  result?: unknown;
  error?: { code: number; message: string };
}

type Content = { type: "text"; text: string } | { type: "image"; data: string; mimeType: string };

interface ToolDefinition {
  name: string;
  description: string;
  schema: z.ZodObject<z.ZodRawShape>;
  /** Only offered when the (simulated) VM backend is enabled. */
  vmOnly?: boolean;
  run(args: any): Promise<Content[]>;
}

const text = (value: unknown): Content[] => [{ type: "text", text: typeof value === "string" ? value : JSON.stringify(value, null, 2) }];

const id = z.string().min(1).describe("Device id");

export class McpServer {
  private readonly tools: ToolDefinition[];

  constructor(
    private readonly orchestrator: OrchestratorService,
    private readonly options: { vmEnabled: boolean } = { vmEnabled: false }
  ) {
    const o = orchestrator;
    this.tools = [
      {
        name: "list_devices",
        description: "List the devices (simulators) in the pool, with status and capacity.",
        schema: z.object({}),
        run: async () => text({ devices: o.listDevices(), capacity: o.capacity() })
      },
      {
        name: "spawn_device",
        description: "Create and boot an iOS simulator. runtime and model accept shorthand such as '18.0' or 'iPhone 15'; omit them for the newest runtime and a current iPhone.",
        schema: z.object({
          name: z.string().optional(),
          runtime: z.string().optional().describe("e.g. '18.0', 'iOS 17.5' or a full runtime identifier"),
          model: z.string().optional().describe("e.g. 'iPhone 15'"),
          type: z.enum(["simulator", "vm"]).optional().describe("'vm' is experimental and simulated"),
          cpu: z.number().optional().describe("vCPU count (vm only)"),
          memory: z.number().optional().describe("Memory in GB (vm only)"),
          wait: z.boolean().optional().describe("Wait until the device is ready (default true)")
        }),
        run: async (a) => text(await o.spawnDevice({ name: a.name, runtime: a.runtime, modelId: a.model, type: a.type, cpu: a.cpu, memory: a.memory, wait: a.wait }))
      },
      { name: "boot_device", description: "Boot a stopped device.", schema: z.object({ id }), run: async (a) => text(await o.bootDevice(a.id)) },
      { name: "shutdown_device", description: "Shut a device down.", schema: z.object({ id }), run: async (a) => text(await o.shutdownDevice(a.id)) },
      {
        name: "delete_device",
        description: "Shut down and permanently delete a device from the host.",
        schema: z.object({ id }),
        run: async (a) => {
          await o.deleteDevice(a.id);
          return text({ deleted: a.id });
        }
      },
      {
        name: "run_test",
        description:
          "Run an Xcode scheme's tests on a simulator. A matching simulator is created automatically when none exists. By default this waits for the result (up to timeoutSeconds).",
        schema: z.object({
          scheme: z.string().optional().describe("Xcode scheme to test"),
          testTarget: z.string().optional().describe("Alias of scheme"),
          project: z.string().optional().describe("Path to the .xcodeproj"),
          workspace: z.string().optional().describe("Path to the .xcworkspace"),
          runtime: z.string().optional(),
          model: z.string().optional(),
          onlyTesting: z.array(z.string()).optional(),
          maxRetries: z.number().int().min(0).max(5).optional(),
          wait: z.boolean().optional().describe("Wait for the result (default true)"),
          timeoutSeconds: z.number().int().min(1).max(3600).optional()
        }),
        run: async (a) => {
          const scheme = a.scheme ?? a.testTarget;
          if (!scheme) throw new Error("scheme is required");
          let job = await o.enqueueTest({
            testTarget: scheme,
            projectPath: a.project,
            workspacePath: a.workspace,
            requiredRuntime: a.runtime,
            requiredModelId: a.model,
            onlyTesting: a.onlyTesting,
            maxRetries: a.maxRetries
          });
          if (a.wait !== false) job = await o.waitForJob(job.id, (a.timeoutSeconds ?? 600) * 1000);
          return text({ job, results: o.getJobResults(job.id) ?? undefined });
        }
      },
      { name: "get_job", description: "Get a test job's status and summary.", schema: z.object({ id: z.string() }), run: async (a) => text({ job: o.getJob(a.id) ?? null, results: o.getJobResults(a.id) ?? undefined }) },
      {
        name: "list_jobs",
        description: "List recent test jobs, newest first.",
        schema: z.object({ status: z.enum(["queued", "running", "retrying", "completed", "failed", "cancelled"]).optional(), limit: z.number().int().min(1).max(100).optional() }),
        run: async (a) => text({ jobs: o.listJobs({ status: a.status, limit: a.limit ?? 20 }) })
      },
      {
        name: "get_job_output",
        description: "Read the tail of a job's raw xcodebuild output.",
        schema: z.object({ id: z.string(), tailBytes: z.number().int().min(1024).max(1_000_000).optional() }),
        run: async (a) => text(o.readJobLog(a.id, a.tailBytes ?? 32_768).text)
      },
      { name: "cancel_job", description: "Cancel a queued or running job.", schema: z.object({ id: z.string() }), run: async (a) => text(await o.cancelJob(a.id)) },
      {
        name: "get_screenshot",
        description: "Capture the current screen of a running device as a PNG image.",
        schema: z.object({ id }),
        run: async (a) => {
          const png = await o.screenshot(a.id);
          return [{ type: "image", data: png.toString("base64"), mimeType: "image/png" }, ...text(`Screenshot of device ${a.id}`)];
        }
      },
      {
        name: "inject_input",
        description: "Send a tap, swipe, keypress or scroll to an experimental (simulated) VM.",
        vmOnly: true,
        schema: z.object({ id, type: z.enum(["tap", "swipe", "keypress", "scroll"]), x: z.number().optional(), y: z.number().optional(), key: z.string().optional() }),
        run: async (a) => text(o.vmEngine.injectInput(a.id, { type: a.type, x: a.x, y: a.y, key: a.key }))
      },
      {
        name: "inject_chaos",
        description: "Record fault-injection settings (network profile, thermal throttling, clock offset) on an experimental (simulated) VM.",
        vmOnly: true,
        schema: z.object({ id, networkProfile: z.enum(["Wi-Fi", "3G", "2G", "No-Network"]).optional(), thermalThrottle: z.boolean().optional(), systemClockOffset: z.number().optional() }),
        run: async (a) => text(o.vmEngine.injectChaos(a.id, a))
      },
      {
        name: "simulate_device_aging",
        description: "Record wear settings (battery degradation, disk fill) on an experimental (simulated) VM.",
        vmOnly: true,
        schema: z.object({ id, batteryDegraded: z.boolean().optional(), diskFullLevel: z.number().min(0).max(100).optional() }),
        run: async (a) => text(o.vmEngine.simulateAging(a.id, a))
      }
    ];
  }

  private visibleTools(): ToolDefinition[] {
    return this.tools.filter((tool) => !tool.vmOnly || this.options.vmEnabled);
  }

  /** Returns undefined for notifications (requests without an id), which must not be answered. */
  async handleRequest(request: McpRequest): Promise<McpResponse | undefined> {
    const isNotification = request.id === undefined;
    const respond = (result: unknown): McpResponse | undefined => (isNotification ? undefined : { jsonrpc: "2.0", id: request.id ?? null, result });
    const fail = (code: number, message: string): McpResponse | undefined => (isNotification ? undefined : { jsonrpc: "2.0", id: request.id ?? null, error: { code, message } });

    try {
      switch (request.method) {
        case "initialize":
          return respond({
            protocolVersion: "2024-11-05",
            capabilities: { tools: {} },
            serverInfo: { name: "mobilelab-mcp-server", version: readVersion() }
          });

        case "ping":
          return respond({});

        case "notifications/initialized":
          return undefined;

        case "tools/list":
          return respond({
            tools: this.visibleTools().map((tool) => {
              const { $schema: _ignored, ...inputSchema } = z.toJSONSchema(tool.schema) as Record<string, unknown>;
              return { name: tool.name, description: tool.description, inputSchema };
            })
          });

        case "tools/call": {
          const params = z.object({ name: z.string(), arguments: z.record(z.string(), z.unknown()).optional() }).safeParse(request.params);
          if (!params.success) return fail(-32602, "tools/call needs { name, arguments }");
          const tool = this.visibleTools().find((t) => t.name === params.data.name);
          if (!tool) return fail(-32602, `Unknown tool: ${params.data.name}`);

          const args = tool.schema.safeParse(params.data.arguments ?? {});
          if (!args.success) {
            return respond({ isError: true, content: text(`Invalid arguments: ${args.error.issues.map((i) => `${i.path.join(".") || "(root)"}: ${i.message}`).join("; ")}`) });
          }
          try {
            return respond({ content: await tool.run(args.data) });
          } catch (error) {
            // A tool that fails is reported to the model as a tool result, not as a protocol error,
            // so the model can read the message and adjust.
            return respond({ isError: true, content: text(errorMessage(error)) });
          }
        }

        default:
          return fail(-32601, `Method not found: ${request.method}`);
      }
    } catch (error) {
      return fail(-32603, errorMessage(error));
    }
  }
}
