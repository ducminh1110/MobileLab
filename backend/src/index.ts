import { createApp } from "./app";
import { loadConfig } from "./config/env";

async function start(): Promise<void> {
  let config;
  try {
    config = loadConfig();
  } catch (error) {
    console.error(error instanceof Error ? error.message : error);
    process.exit(2);
  }

  const { app, services, close } = await createApp(config);

  let stopping = false;
  const stop = async (signal: string) => {
    if (stopping) return;
    stopping = true;
    app.log.info(`${signal} received, shutting down`);
    try {
      await close();
    } finally {
      process.exit(0);
    }
  };
  process.on("SIGINT", () => void stop("SIGINT"));
  process.on("SIGTERM", () => void stop("SIGTERM"));

  try {
    await app.listen({ host: config.host, port: config.port });
  } catch (error) {
    app.log.error(error);
    process.exit(1);
  }

  const url = `http://${config.host === "0.0.0.0" ? "127.0.0.1" : config.host}:${config.port}`;
  app.log.info(`MobileLab backend ${services.config.mock ? "(DEMO MODE: simulator commands are simulated) " : ""}ready at ${url}`);
  if (config.mock) {
    app.log.warn(
      config.mockReason === "auto"
        ? "Not running on macOS: simulator commands are simulated and test results are not real. Set IOSLAB_SIMULATOR_MOCK=false on a Mac to use real simulators."
        : "IOSLAB_SIMULATOR_MOCK is set: simulator commands are simulated and test results are not real."
    );
  }
  if (!config.apiToken && !["127.0.0.1", "localhost", "::1"].includes(config.host)) {
    app.log.warn(`Listening on ${config.host} with no IOSLAB_API_TOKEN. Anyone who can reach this port can run commands on this machine.`);
  }
}

void start();
