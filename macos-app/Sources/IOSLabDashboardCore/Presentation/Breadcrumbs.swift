import Foundation

/// One crumb of the jump bar. A crumb with siblings is a popup that lists them.
public struct Crumb: Equatable, Sendable, Identifiable {
    public var id: String
    public var title: String
    public var icon: NodeIcon
    /// What clicking the crumb opens; nil for plain group crumbs.
    public var target: EditorTarget?
    public var siblings: [Crumb]

    public init(id: String, title: String, icon: NodeIcon = .none, target: EditorTarget? = nil, siblings: [Crumb] = []) {
        self.id = id
        self.title = title
        self.icon = icon
        self.target = target
        self.siblings = siblings
    }
}

public enum Breadcrumbs {
    static let siblingLimit = 12

    /// `MobileLab > Simulators > iOS 18.0 > iPhone 15`, `MobileLab > Reports > DemoApp > Tests`, `MobileLab > Runs > Matrix`.
    public static func build(
        target: EditorTarget, tab: ReportTab, state: DashboardState, now: Date, formatting: DateFormatting, catalog: Catalog? = nil
    ) -> [Crumb] {
        let root = Crumb(id: "crumb:root", title: "MobileLab", icon: .folder, target: .welcome)

        switch target {
        case .welcome:
            return [root]

        case .device(let id):
            guard let device = state.device(id: id) else { return [root, Crumb(id: "crumb:device:\(id)", title: "Unknown device", icon: .iphone)] }
            if device.isVM {
                return [root, Crumb(id: "crumb:vms", title: "Experimental VMs", icon: .folder), deviceCrumb(device, state: state)]
            }
            return [
                root,
                Crumb(id: "crumb:simulators", title: "Simulators", icon: .folder),
                Crumb(id: "crumb:runtime:\(device.runtime)", title: device.runtimeDisplayName, icon: .folder),
                deviceCrumb(device, state: state)
            ]

        case .job(let id):
            guard let job = state.job(id: id) else { return [root, Crumb(id: "crumb:job:\(id)", title: "Unknown job", icon: .diamond(.notRun))] }
            let siblings = state.jobs.filter { $0.testTarget == job.testTarget && $0.id != job.id }.prefix(siblingLimit).map { other in
                Crumb(id: "crumb:job:\(other.id)", title: siblingTitle(other, now: now, formatting: formatting), icon: .diamond(JobText.glyph(other.status)), target: .job(other.id))
            }
            return [
                root,
                Crumb(id: "crumb:reports", title: "Reports", icon: .folder),
                Crumb(id: "crumb:job:\(job.id)", title: job.testTarget, icon: .diamond(JobText.glyph(job.status)), target: .job(job.id), siblings: siblings),
                Crumb(id: "crumb:tab:\(tab.rawValue)", title: tab.title, icon: .none)
            ]

        case .run(let id):
            let title = state.run(id: id)?.title ?? "Unknown run"
            return [root, Crumb(id: "crumb:runs", title: "Runs", icon: .runs), Crumb(id: "crumb:run:\(id)", title: title, icon: .diamond(state.run(id: id).map { JobText.glyph($0.status) } ?? .notRun), target: .run(id))]
        }
    }

    private static func deviceCrumb(_ device: Device, state: DashboardState) -> Crumb {
        let siblings = state.devices.filter { $0.runtime == device.runtime && $0.id != device.id && $0.type == device.type }.prefix(siblingLimit).map {
            Crumb(id: "crumb:device:\($0.id)", title: $0.name, icon: $0.isTablet ? .ipad : .iphone, target: .device($0.id))
        }
        return Crumb(id: "crumb:device:\(device.id)", title: device.name, icon: device.isVM ? .vm : (device.isTablet ? .ipad : .iphone), target: .device(device.id), siblings: siblings)
    }

    private static func siblingTitle(_ job: TestJob, now: Date, formatting: DateFormatting) -> String {
        "\(job.testTarget), \(formatting.dayAndTime(job.finishedOrUpdatedAt, now: now))"
    }
}
