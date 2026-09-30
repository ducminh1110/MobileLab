#pragma once
// Handles to the Android core shared by every view. Nothing here owns anything.
#include <QString>

class AndroidEmulator;
class AndroidRuntime;
class ResourceScheduler;
class MatrixExecutor;
class ArtifactCollector;
class AndroidContainerRuntime;
class ApiServer;
class ActivityLog;
class HostMetrics;

struct AppContext {
    AndroidEmulator *emulator = nullptr;
    AndroidRuntime *runtime = nullptr;
    ResourceScheduler *scheduler = nullptr;
    MatrixExecutor *matrix = nullptr;
    ArtifactCollector *artifacts = nullptr;
    AndroidContainerRuntime *container = nullptr;
    ApiServer *api = nullptr;
    ActivityLog *activity = nullptr;
    HostMetrics *host = nullptr;
    QString apiError;        // non empty when the REST server could not bind
    bool containerProbed = false;
    bool containerViable = false;
};
