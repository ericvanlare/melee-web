import AppKit
import CryptoKit
import Foundation
import Darwin

/// The desktop front end for the reference-capture environment.
///
/// The capture worker is deliberately a separate process.  The GUI never guesses
/// that a disc, controller or capture is ready: those facts are displayed only
/// after the corresponding JSONL event has been received from the capture worker.
@main
final class ReferenceCaptureApplication: NSObject, NSApplicationDelegate {
    private var windowController: CaptureWindowController?

    // There is no storyboard or nib in the packaged app.  Start AppKit
    // explicitly so the delegate and its window controller are retained for
    // the entire event loop.
    static func main() {
        let arguments = CommandLine.arguments
        if let flag = arguments.firstIndex(of: "--create-desktop-alias") {
            let status: Int32
            do {
                guard arguments.indices.contains(flag + 1) else {
                    throw AliasError.invalidDestination("--create-desktop-alias requires a destination path")
                }
                try createDesktopAlias(at: URL(fileURLWithPath: arguments[flag + 1]))
                status = 0
            } catch {
                let message = "WebMelee Reference Capture: \(error.localizedDescription)\n"
                FileHandle.standardError.write(Data(message.utf8))
                status = 1
            }
            exit(status)
        }
        let application = NSApplication.shared
        let delegate = ReferenceCaptureApplication()
        application.delegate = delegate
        application.setActivationPolicy(.regular)

        let applicationMenu = NSMenu()
        let applicationItem = NSMenuItem()
        applicationMenu.addItem(applicationItem)
        let submenu = NSMenu(title: "WebMelee Reference Capture")
        submenu.addItem(withTitle: "Quit WebMelee Reference Capture",
                        action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        applicationItem.submenu = submenu
        application.mainMenu = applicationMenu
        application.run()
    }

    private static func createDesktopAlias(at destination: URL) throws {
        let fileManager = FileManager.default
        let destination = destination.standardizedFileURL
        let target = URL(fileURLWithPath: Bundle.main.bundlePath).standardizedFileURL
        guard target.pathExtension == "app", fileManager.fileExists(atPath: target.path) else {
            throw AliasError.invalidDestination("The installed application bundle is unavailable")
        }
        if fileManager.fileExists(atPath: destination.path) {
            let values = try destination.resourceValues(forKeys: [.isAliasFileKey])
            guard values.isAliasFile == true else {
                throw AliasError.existingItem("A non-alias Desktop item already uses this application name")
            }
            try fileManager.removeItem(at: destination)
        }
        let bookmark = try target.bookmarkData(options: .suitableForBookmarkFile,
                                               includingResourceValuesForKeys: nil,
                                               relativeTo: nil)
        try URL.writeBookmarkData(bookmark, to: destination)
        let values = try destination.resourceValues(forKeys: [.isAliasFileKey])
        guard values.isAliasFile == true else {
            try? fileManager.removeItem(at: destination)
            throw AliasError.invalidDestination("The Desktop item was not created as a Finder alias")
        }
    }

    private enum AliasError: LocalizedError {
        case invalidDestination(String)
        case existingItem(String)

        var errorDescription: String? {
            switch self {
            case .invalidDestination(let message), .existingItem(let message): return message
            }
        }
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        let controller = CaptureWindowController()
        windowController = controller
        controller.showWindow(nil)
        controller.launchBackend()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }

    func applicationWillTerminate(_ notification: Notification) {
        windowController?.shutdown()
    }
}

private struct AppIdentity: Decodable {
    let schema: String
    let application: String
    let version: String
    let appCodeSHA256: String
    let pythonRuntimeSHA256: String
    let runtimeFiles: [RuntimeFile]

    enum CodingKeys: String, CodingKey {
        case schema
        case application
        case version
        case appCodeSHA256 = "app_code_sha256"
        case pythonRuntimeSHA256 = "python_runtime_sha256"
        case runtimeFiles = "runtime_files"
    }
}

private struct RuntimeFile: Decodable {
    let path: String
    let sha256: String
    let bytes: Int
}

private final class BackendClient {
    var onEvent: (([String: Any]) -> Void)?
    var onLog: ((String) -> Void)?
    var onExit: ((String) -> Void)?

    private(set) var process: Process?
    private var inputPipe: Pipe?
    private let writeQueue = DispatchQueue(label: "org.webmelee.reference-capture.backend-write")

    func launch(bundle: Bundle, configURL: URL) throws {
        guard process == nil else { return }
        guard let resourceURL = bundle.resourceURL else {
            throw BackendError.missingResource("The application resources are missing.")
        }
        let runtimeURL = resourceURL.appendingPathComponent("runtime", isDirectory: true)
        let scriptURL = runtimeURL.appendingPathComponent("scripts/reference_capture_app.py")
        let pythonManifestURL = resourceURL.appendingPathComponent("python-runtime.json")
        guard FileManager.default.isReadableFile(atPath: scriptURL.path) else {
            throw BackendError.missingResource("Capture support is not installed.")
        }
        guard FileManager.default.isReadableFile(atPath: pythonManifestURL.path) else {
            throw BackendError.missingResource("The pinned Python runtime manifest is missing.")
        }

        let manifestData = try Data(contentsOf: pythonManifestURL)
        guard let manifest = try JSONSerialization.jsonObject(with: manifestData) as? [String: Any],
              let pythonPath = manifest["path"] as? String,
              !pythonPath.isEmpty else {
            throw BackendError.invalidResource("The pinned Python runtime manifest is invalid.")
        }
        let pythonURL = URL(fileURLWithPath: pythonPath).standardizedFileURL
        guard FileManager.default.isExecutableFile(atPath: pythonURL.path) else {
            throw BackendError.invalidResource("The pinned Python executable is unavailable at \(pythonURL.path).")
        }
        if let expectedHash = manifest["sha256"] as? String,
           !expectedHash.isEmpty,
           sha256(URL(fileURLWithPath: pythonURL.path)) != expectedHash {
            throw BackendError.invalidResource("The pinned Python executable changed after installation.")
        }

        let process = Process()
        let input = Pipe()
        let output = Pipe()
        let errors = Pipe()
        process.executableURL = pythonURL
        process.arguments = ["-u", scriptURL.path, "--gui"]
        process.currentDirectoryURL = runtimeURL
        var environment = ProcessInfo.processInfo.environment
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        environment["WEBMELEE_REFERENCE_CAPTURE_CONFIG"] = configURL.path
        environment["WEBMELEE_REFERENCE_CAPTURE_SETTINGS"] = configURL.path
        environment["WEBMELEE_REFERENCE_CAPTURE_APP_BUNDLE"] = bundle.bundlePath
        process.environment = environment
        process.standardInput = input
        process.standardOutput = output
        process.standardError = errors
        process.terminationHandler = { [weak self] task in
            let reason = task.terminationReason == .uncaughtSignal ? "terminated by signal" : "exited (\(task.terminationStatus))"
            DispatchQueue.main.async {
                self?.onExit?(reason)
            }
        }

        inputPipe = input
        self.process = process
        process.launch()
        readLines(from: output.fileHandleForReading, isError: false)
        readLines(from: errors.fileHandleForReading, isError: true)
    }

    func send(command: String) {
        send(command: command, bundlePath: nil)
    }

    func send(command: String, bundlePath: URL?) {
        guard let input = inputPipe, process?.isRunning == true else { return }
        var object: [String: Any] = ["command": command]
        if let bundlePath {
            object["bundle_path"] = bundlePath.standardizedFileURL.path
        }
        guard let data = try? JSONSerialization.data(withJSONObject: object),
              var line = String(data: data, encoding: .utf8) else { return }
        line.append("\n")
        guard let bytes = line.data(using: .utf8) else { return }
        writeQueue.async {
            input.fileHandleForWriting.write(bytes)
        }
    }

    func terminate() {
        guard let process, process.isRunning else { return }
        process.terminate()
        // A filesystem read can wait on an unanswered macOS permission prompt.
        // Give the supervisor time to stop its Dolphin process and preserve a
        // partial, then reap only our child so relaunch cannot inherit its lock.
        let deadline = Date().addingTimeInterval(15)
        while process.isRunning && Date() < deadline {
            Thread.sleep(forTimeInterval: 0.05)
        }
        if process.isRunning { kill(process.processIdentifier, SIGKILL) }
    }

    private func readLines(from handle: FileHandle, isError: Bool) {
        DispatchQueue.global(qos: .utility).async { [weak self] in
            var buffer = Data()
            while true {
                // ``readData(ofLength:)`` can wait for the entire requested
                // chunk on a pipe.  The backend emits short JSONL status rows,
                // so consume whatever is currently available to keep the UI
                // responsive between events.
                let chunk = handle.availableData
                if chunk.isEmpty { break }
                buffer.append(chunk)
                while let newline = buffer.firstIndex(of: 0x0a) {
                    let lineData = buffer.prefix(upTo: newline)
                    buffer.removeSubrange(...newline)
                    guard let line = String(data: lineData, encoding: .utf8), !line.isEmpty else { continue }
                    if isError {
                        DispatchQueue.main.async { self?.onLog?(line) }
                    } else if let data = line.data(using: .utf8),
                              let value = try? JSONSerialization.jsonObject(with: data),
                              let event = value as? [String: Any] {
                        DispatchQueue.main.async { self?.onEvent?(event) }
                    } else {
                        DispatchQueue.main.async { self?.onLog?(line) }
                    }
                }
            }
            if !buffer.isEmpty, let line = String(data: buffer, encoding: .utf8) {
                DispatchQueue.main.async { self?.onLog?(line) }
            }
        }
    }

    private func sha256(_ url: URL) -> String? {
        guard let data = try? Data(contentsOf: url) else { return nil }
        return SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    enum BackendError: LocalizedError {
        case missingResource(String)
        case invalidResource(String)

        var errorDescription: String? {
            switch self {
            case .missingResource(let message), .invalidResource(let message): return message
            }
        }
    }
}

private final class CaptureWindowController: NSWindowController {
    private let client = BackendClient()
    private let captureViewController = CaptureViewController()
    private var isConfigured = false

    convenience init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 760, height: 620),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        self.init(window: window)
        window.title = "WebMelee Reference Capture"
        window.minSize = NSSize(width: 390, height: 560)
        window.setContentSize(NSSize(width: 760, height: 620))
        window.center()
        configureWindow()
    }

    override func windowDidLoad() {
        super.windowDidLoad()
        configureWindow()
    }

    private func configureWindow() {
        guard !isConfigured else { return }
        isConfigured = true
        contentViewController = captureViewController
        client.onEvent = { [weak self] event in self?.captureViewController.apply(event: event) }
        client.onLog = { [weak self] line in self?.captureViewController.apply(log: line) }
        client.onExit = { [weak self] reason in self?.captureViewController.apply(exitReason: reason) }
        captureViewController.onCommand = { [weak self] command in self?.send(command: command) }
        captureViewController.onReplay = { [weak self] bundle in
            self?.send(command: "replay", bundlePath: bundle)
        }
        captureViewController.onOpenURL = { url in NSWorkspace.shared.open(url) }
    }

    func launchBackend() {
        guard let window else { return }
        do {
            try verifyIdentity()
            let configURL = try ensurePrivateConfiguration()
            try client.launch(bundle: .main, configURL: configURL)
            captureViewController.apply(localMessage: "Starting verification…")
            client.send(command: "verify")
        } catch {
            captureViewController.apply(failure: error.localizedDescription)
            window.makeKeyAndOrderFront(nil)
        }
    }

    func shutdown() {
        client.terminate()
    }

    private func send(command: String, bundlePath: URL? = nil) {
        if command == "verify" && client.process == nil {
            launchBackend()
            return
        }
        client.send(command: command, bundlePath: bundlePath)
    }

    private func verifyIdentity() throws {
        guard let manifestURL = Bundle.main.resourceURL?.appendingPathComponent("identity.json"),
              let data = try? Data(contentsOf: manifestURL) else {
            throw BackendClient.BackendError.missingResource("The application identity manifest is missing.")
        }
        let identity = try JSONDecoder().decode(AppIdentity.self, from: data)
        guard identity.schema == "webmelee-reference-capture-identity-v1" else {
            throw BackendClient.BackendError.invalidResource("The application identity schema is unsupported.")
        }
        guard identity.application == "WebMelee Reference Capture" else {
            throw BackendClient.BackendError.invalidResource("The application identity is for a different application.")
        }
        guard let executableURL = Bundle.main.executableURL,
              let executableData = try? Data(contentsOf: executableURL) else {
            throw BackendClient.BackendError.invalidResource("The application executable cannot be read.")
        }
        let actual = SHA256.hash(data: executableData).map { String(format: "%02x", $0) }.joined()
        guard actual == identity.appCodeSHA256 else {
            throw BackendClient.BackendError.invalidResource("The application code hash does not match its identity manifest.")
        }
        try verifyRuntime(identity: identity)
    }

    private func verifyRuntime(identity: AppIdentity) throws {
        guard let resources = Bundle.main.resourceURL else {
            throw BackendClient.BackendError.missingResource("The application resources are missing.")
        }
        let runtime = resources.appendingPathComponent("runtime", isDirectory: true).standardizedFileURL
        let fileManager = FileManager.default
        let runtimePrefix = runtime.path.hasSuffix("/") ? runtime.path : runtime.path + "/"
        var observed: [String: URL] = [:]
        guard let enumerator = fileManager.enumerator(
            at: runtime,
            includingPropertiesForKeys: [.isRegularFileKey, .isSymbolicLinkKey],
            options: []
        ) else {
            throw BackendClient.BackendError.missingResource("The bundled capture runtime is missing.")
        }
        for case let item as URL in enumerator {
            let values = try item.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
            if values.isSymbolicLink == true {
                throw BackendClient.BackendError.invalidResource("The bundled capture runtime contains a symbolic link.")
            }
            if values.isRegularFile == true {
                guard item.standardizedFileURL.path.hasPrefix(runtimePrefix) else {
                    throw BackendClient.BackendError.invalidResource("The bundled capture runtime escapes its resource directory.")
                }
                let relative = String(item.standardizedFileURL.path.dropFirst(runtimePrefix.count))
                observed[relative] = item
            }
        }
        var expected: [String: RuntimeFile] = [:]
        for entry in identity.runtimeFiles {
            guard !entry.path.isEmpty,
                  !entry.path.hasPrefix("/"),
                  !entry.path.split(separator: "/").contains(".."),
                  entry.path != "." else {
                throw BackendClient.BackendError.invalidResource("The runtime identity contains an unsafe path.")
            }
            guard expected[entry.path] == nil,
                  entry.sha256.count == 64,
                  entry.bytes >= 0 else {
                throw BackendClient.BackendError.invalidResource("The runtime identity contains a duplicate or invalid file record.")
            }
            expected[entry.path] = entry
        }
        guard Set(expected.keys) == Set(observed.keys) else {
            throw BackendClient.BackendError.invalidResource("The bundled capture runtime differs from its identity manifest.")
        }
        for (relative, entry) in expected {
            guard let url = observed[relative],
                  let data = try? Data(contentsOf: url),
                  data.count == entry.bytes else {
                throw BackendClient.BackendError.invalidResource("The bundled runtime file size changed: \(relative)")
            }
            let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
            guard digest == entry.sha256.lowercased() else {
                throw BackendClient.BackendError.invalidResource("The bundled runtime file hash changed: \(relative)")
            }
        }
        let pythonManifestURL = resources.appendingPathComponent("python-runtime.json")
        guard let pythonManifestData = try? Data(contentsOf: pythonManifestURL),
              SHA256.hash(data: pythonManifestData).map({ String(format: "%02x", $0) }).joined() == identity.pythonRuntimeSHA256.lowercased(),
              let pythonManifest = try? JSONSerialization.jsonObject(with: pythonManifestData) as? [String: Any],
              let pythonPath = pythonManifest["path"] as? String,
              !pythonPath.isEmpty,
              let pythonHash = pythonManifest["sha256"] as? String,
              pythonHash.count == 64,
              let pythonVersion = pythonManifest["version"] as? String,
              !pythonVersion.isEmpty else {
            throw BackendClient.BackendError.invalidResource("The pinned Python runtime manifest is missing or not identity-bound.")
        }
        _ = pythonPath
        _ = pythonHash
        _ = pythonVersion
    }

    private func ensurePrivateConfiguration() throws -> URL {
        let appSupport = try FileManager.default.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: true
        )
        let root = appSupport.appendingPathComponent("WebMelee Reference Capture", isDirectory: true)
        let profile = root.appendingPathComponent("Configuration", isDirectory: true)
        let captures = root.appendingPathComponent("Captures", isDirectory: true)
        try FileManager.default.createDirectory(at: profile, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: captures, withIntermediateDirectories: true)
        // The environment verifier owns this file.  Creating a guessed
        // settings record here would make an unconfigured machine look valid
        // and could overwrite an operator's private profile.
        return root.appendingPathComponent("environment.json")
    }
}

private final class FlippedView: NSView {
    override var isFlipped: Bool { true }
}

private final class AdaptiveButtonStack: NSStackView {
    override func layout() {
        // Switch before the four buttons' horizontal minimum can prevent the
        // operator from narrowing the window enough to trigger this layout.
        let desired: NSUserInterfaceLayoutOrientation = bounds.width < 620 ? .vertical : .horizontal
        if orientation != desired {
            orientation = desired
        }
        super.layout()
    }
}

private final class AdaptiveProgressStack: NSStackView {
    override func layout() {
        let vertical = bounds.width < 620
        let desired: NSUserInterfaceLayoutOrientation = vertical ? .vertical : .horizontal
        if orientation != desired {
            orientation = desired
        }
        alignment = vertical ? .width : .centerY
        super.layout()
    }
}

private final class CaptureViewController: NSViewController {
    var onCommand: ((String) -> Void)?
    var onReplay: ((URL) -> Void)?
    var onOpenURL: ((URL) -> Void)?

    private let stateValue = NSTextField(labelWithString: "Waiting for verification")
    private let stateDescription = NSTextField(labelWithString: "The environment has not reported readiness.")
    private let environmentValue = NSTextField(labelWithString: "Waiting for verification")
    private let discValue = NSTextField(labelWithString: "Unknown")
    private let controllerValue = NSTextField(labelWithString: "Unknown")
    private let dolphinValue = NSTextField(labelWithString: "Unknown")
    private let captureValue = NSTextField(labelWithString: "Idle")
    private let frameValue = NSTextField(labelWithString: "0")
    private let eventValue = NSTextField(labelWithString: "0")
    private let elapsedValue = NSTextField(labelWithString: "Elapsed: —")
    private let captureDestinationValue = NSTextField(labelWithString: "No capture folder selected")
    private let bundleValue = NSTextField(labelWithString: "No capture bundle yet")
    private let progress = NSProgressIndicator()
    private let startButton = NSButton(title: "Start Capture", target: nil, action: nil)
    private let stopButton = NSButton(title: "Stop Capture", target: nil, action: nil)
    private let configureButton = NSButton(title: "Configure Controller…", target: nil, action: nil)
    private let rescanButton = NSButton(title: "Rescan", target: nil, action: nil)
    private let openButton = NSButton(title: "Open Capture Folder", target: nil, action: nil)
    private let replayButton = NSButton(title: "Replay Capture…", target: nil, action: nil)
    private let logValue = NSTextField(labelWithString: "")

    private var state = "unknown"
    private var controller = "unknown"
    private var discAccepted = false
    private var latestBundlePath: URL?
    private var captureDestinationURL: URL?

    override func loadView() {
        let root = FlippedView()
        root.wantsLayer = true
        root.layer?.backgroundColor = NSColor.windowBackgroundColor.cgColor
        view = root

        let scroll = NSScrollView()
        scroll.translatesAutoresizingMaskIntoConstraints = false
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        root.addSubview(scroll)

        let content = FlippedView()
        content.translatesAutoresizingMaskIntoConstraints = false
        scroll.documentView = content
        NSLayoutConstraint.activate([
            scroll.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            scroll.topAnchor.constraint(equalTo: root.topAnchor),
            scroll.bottomAnchor.constraint(equalTo: root.bottomAnchor),
            content.leadingAnchor.constraint(equalTo: scroll.contentView.leadingAnchor),
            content.trailingAnchor.constraint(equalTo: scroll.contentView.trailingAnchor),
            content.topAnchor.constraint(equalTo: scroll.contentView.topAnchor),
            content.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor)
        ])

        let title = NSTextField(labelWithString: "Reference Capture")
        title.font = .systemFont(ofSize: 26, weight: .semibold)
        let subtitle = NSTextField(labelWithString: "A private workspace for original-game reference captures.")
        subtitle.textColor = .secondaryLabelColor
        subtitle.lineBreakMode = .byWordWrapping
        subtitle.maximumNumberOfLines = 2
        subtitle.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        stateValue.font = .systemFont(ofSize: 17, weight: .semibold)
        stateDescription.textColor = .secondaryLabelColor
        stateDescription.lineBreakMode = .byWordWrapping
        stateDescription.maximumNumberOfLines = 0
        stateDescription.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let stateBox = NSStackView(views: [stateValue, stateDescription])
        stateBox.orientation = .vertical
        stateBox.alignment = .leading
        stateBox.spacing = 4
        stateBox.edgeInsets = NSEdgeInsets(top: 14, left: 16, bottom: 14, right: 16)
        stateBox.wantsLayer = true
        stateBox.layer?.cornerRadius = 10
        stateBox.layer?.backgroundColor = NSColor.controlAccentColor.withAlphaComponent(0.12).cgColor

        let rows = NSStackView()
        rows.orientation = .vertical
        rows.alignment = .leading
        rows.spacing = 1
        let statusRows = [
            statusRow("Environment", stateValue: environmentValue, initial: "Waiting for verification"),
            statusRow("Accepted disc", stateValue: discValue, initial: "Unknown"),
            statusRow("Physical controller", stateValue: controllerValue, initial: "Unknown"),
            statusRow("Dolphin", stateValue: dolphinValue, initial: "Unknown"),
            statusRow("Capture", stateValue: captureValue, initial: "Idle"),
        ]
        for row in statusRows {
            rows.addArrangedSubview(row)
            row.widthAnchor.constraint(equalTo: rows.widthAnchor).isActive = true
        }

        let progressTitle = NSTextField(labelWithString: "Capture progress")
        progressTitle.font = .systemFont(ofSize: 13, weight: .medium)
        progress.isIndeterminate = false
        progress.minValue = 0
        progress.maxValue = 1
        progress.doubleValue = 0
        progress.controlSize = .small
        let progressRow = AdaptiveProgressStack(views: [progressTitle, progress, frameValue, eventValue, elapsedValue])
        progressRow.orientation = .horizontal
        progressRow.alignment = .centerY
        progressRow.spacing = 10
        progressRow.setHuggingPriority(.required, for: .horizontal)
        frameValue.font = .monospacedDigitSystemFont(ofSize: 12, weight: .regular)
        eventValue.font = .monospacedDigitSystemFont(ofSize: 12, weight: .regular)
        elapsedValue.font = .monospacedDigitSystemFont(ofSize: 12, weight: .regular)

        bundleValue.textColor = .secondaryLabelColor
        bundleValue.lineBreakMode = .byWordWrapping
        bundleValue.maximumNumberOfLines = 3
        bundleValue.usesSingleLineMode = false
        bundleValue.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let bundleRow = pathRow("Bundle", value: bundleValue)

        captureDestinationValue.textColor = .secondaryLabelColor
        captureDestinationValue.lineBreakMode = .byWordWrapping
        captureDestinationValue.maximumNumberOfLines = 3
        captureDestinationValue.usesSingleLineMode = false
        captureDestinationValue.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let captureDestinationRow = pathRow("Capture folder", value: captureDestinationValue)

        startButton.bezelStyle = .rounded
        startButton.keyEquivalent = "\r"
        startButton.target = self
        startButton.action = #selector(startCapture)
        stopButton.bezelStyle = .rounded
        stopButton.target = self
        stopButton.action = #selector(stopCapture)
        configureButton.bezelStyle = .rounded
        configureButton.target = self
        configureButton.action = #selector(configureController)
        rescanButton.bezelStyle = .rounded
        rescanButton.target = self
        rescanButton.action = #selector(rescan)
        openButton.bezelStyle = .rounded
        openButton.target = self
        openButton.action = #selector(openInbox)
        replayButton.bezelStyle = .rounded
        replayButton.target = self
        replayButton.action = #selector(replayCapture)
        let primaryButtons = NSStackView(views: [startButton, stopButton])
        primaryButtons.orientation = .horizontal
        primaryButtons.spacing = 10
        let secondaryButtons = AdaptiveButtonStack(views: [configureButton, rescanButton, openButton, replayButton])
        secondaryButtons.orientation = .horizontal
        secondaryButtons.spacing = 10
        secondaryButtons.distribution = .fillProportionally

        logValue.textColor = .secondaryLabelColor
        logValue.font = .systemFont(ofSize: 11)
        logValue.lineBreakMode = .byWordWrapping
        logValue.maximumNumberOfLines = 0
        logValue.usesSingleLineMode = false
        let footer = NSTextField(labelWithString: "Captures and configuration stay in your private Application Support folder.")
        footer.textColor = .tertiaryLabelColor
        footer.font = .systemFont(ofSize: 11)
        footer.lineBreakMode = .byWordWrapping
        footer.maximumNumberOfLines = 2

        let arrangedViews: [NSView] = [title, subtitle, stateBox, rows, progressRow,
                                       captureDestinationRow, bundleRow, primaryButtons,
                                       secondaryButtons, logValue, footer]
        let stack = NSStackView(views: arrangedViews)
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.distribution = .fill
        stack.spacing = 14
        content.addSubview(stack)
        for arrangedView in arrangedViews {
            arrangedView.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true
        }
        stateValue.widthAnchor.constraint(equalTo: stateBox.widthAnchor, constant: -32).isActive = true
        stateDescription.widthAnchor.constraint(equalTo: stateBox.widthAnchor, constant: -32).isActive = true
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 24),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -24),
            stack.topAnchor.constraint(equalTo: content.topAnchor, constant: 24),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor, constant: -24),
            progress.widthAnchor.constraint(greaterThanOrEqualToConstant: 40),
            startButton.widthAnchor.constraint(greaterThanOrEqualToConstant: 130),
            stopButton.widthAnchor.constraint(greaterThanOrEqualToConstant: 110)
        ])
        updateButtons()
    }

    func launchBackend() {
        // The window controller owns process startup.  This method is kept as a
        // no-op hook so the view remains easy to host in a test harness.
    }

    func apply(event: [String: Any]) {
        if let value = event["state"] as? String { state = value }
        if event.keys.contains("accepted_disc") {
            discAccepted = (event["accepted_disc"] as? Bool) ?? false
        }
        if let value = event["physical_controller"] as? String { controller = value }
        if let value = event["dolphin"] as? String { dolphinValue.stringValue = display(value) }
        if let value = event["capture"] as? String { captureValue.stringValue = display(value) }
        environmentValue.stringValue = display(state)
        if let value = event["source_frames"] as? NSNumber { frameValue.stringValue = "Source frames: \(value.intValue)" }
        if let value = event["events"] as? NSNumber { eventValue.stringValue = "Observed events: \(value.intValue)" }
        if let value = (event["elapsed_seconds"] ?? event["elapsed"]) as? NSNumber {
            elapsedValue.stringValue = String(format: "Elapsed: %.1fs", value.doubleValue)
        }
        if let value = event["event_progress"] as? NSNumber { progress.doubleValue = max(0, min(1, value.doubleValue)) }
        if let value = event["bundle_path"] as? String, !value.isEmpty {
            latestBundlePath = URL(fileURLWithPath: value)
            bundleValue.stringValue = value
        }
        if let value = event["capture_destination"] as? String, !value.isEmpty {
            captureDestinationURL = URL(fileURLWithPath: value)
            captureDestinationValue.stringValue = value
        }
        if let message = event["message"] as? String { logValue.stringValue = message }
        if let error = event["error"] as? String { logValue.stringValue = error }
        controllerValue.stringValue = display(controller)
        if event.keys.contains("accepted_disc") {
            discValue.stringValue = event["accepted_disc"] as? Bool == true ? "Accepted" :
                ((event["accepted_disc"] is NSNull) ? "Unknown" : "Rejected")
        } else if discAccepted {
            discValue.stringValue = "Accepted"
        }
        stateValue.stringValue = display(state)
        stateDescription.stringValue = descriptionForState()
        updateButtons()
    }

    func apply(log: String) {
        logValue.stringValue = log
    }

    func apply(exitReason: String) {
        state = "failed"
        stateValue.stringValue = "Capture stopped"
        stateDescription.stringValue = "The capture service \(exitReason). Start the app again to retry."
        logValue.stringValue = "No capture can be started until verification is available."
        updateButtons()
    }

    func apply(localMessage: String) {
        logValue.stringValue = localMessage
    }

    func apply(failure: String) {
        state = "failed"
        stateValue.stringValue = "Setup failed"
        stateDescription.stringValue = failure
        logValue.stringValue = "Fix the installation and relaunch the application."
        updateButtons()
    }

    @objc private func startCapture() { onCommand?("start") }
    @objc private func stopCapture() { onCommand?("stop") }
    @objc private func configureController() { onCommand?("configure_controller") }
    @objc private func rescan() { onCommand?("verify") }
    @objc private func replayCapture() {
        guard let window = view.window else { return }
        let panel = NSOpenPanel()
        panel.title = "Replay Capture"
        panel.message = "Choose an accepted or ingested finalized recording from Captures."
        panel.prompt = "Replay"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        if let captures = capturesRootURL(), FileManager.default.fileExists(atPath: captures.path) {
            panel.directoryURL = captures
        }
        panel.beginSheetModal(for: window) { [weak self] response in
            guard response == .OK, let bundle = panel.url else { return }
            self?.onReplay?(bundle.standardizedFileURL)
        }
    }
    @objc private func openInbox() {
        onCommand?("open_inbox")
        if let latestBundlePath {
            onOpenURL?(latestBundlePath.deletingLastPathComponent())
        } else if let captureDestinationURL {
            onOpenURL?(captureDestinationURL)
        } else if let appSupport = try? FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: false) {
            onOpenURL?(appSupport.appendingPathComponent("WebMelee Reference Capture/Captures", isDirectory: true))
        }
    }

    private func statusRow(_ title: String, stateValue: NSTextField, initial: String) -> NSView {
        let titleField = NSTextField(labelWithString: title)
        titleField.textColor = .secondaryLabelColor
        titleField.alignment = .left
        titleField.setContentHuggingPriority(.required, for: .horizontal)
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        spacer.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        stateValue.alignment = .right
        stateValue.setContentHuggingPriority(.required, for: .horizontal)
        stateValue.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        stateValue.stringValue = initial
        let row = NSStackView(views: [titleField, spacer, stateValue])
        row.orientation = .horizontal
        row.alignment = .centerY
        row.spacing = 12
        row.distribution = .fill
        row.edgeInsets = NSEdgeInsets(top: 7, left: 0, bottom: 7, right: 0)
        return row
    }

    private func pathRow(_ title: String, value: NSTextField) -> NSView {
        let titleField = NSTextField(labelWithString: title)
        titleField.textColor = .secondaryLabelColor
        titleField.alignment = .left
        titleField.setContentHuggingPriority(.required, for: .horizontal)
        value.alignment = .left
        value.setContentHuggingPriority(.defaultLow, for: .horizontal)
        value.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let row = NSStackView(views: [titleField, value])
        row.orientation = .horizontal
        row.alignment = .top
        row.spacing = 10
        row.distribution = .fill
        return row
    }

    private func updateButtons() {
        let canStart = state == "ready" && discAccepted && controller == "connected"
        let busy = ["verifying", "configuring", "starting", "running", "recording",
                    "stopping", "finalizing", "replay", "replaying"].contains(state)
        let captureActive = ["starting", "running", "recording", "stopping", "finalizing"].contains(state)
        let canReplay = discAccepted && !busy
        startButton.isEnabled = canStart
        stopButton.isEnabled = captureActive
        configureButton.isEnabled = !busy
        rescanButton.isEnabled = !busy
        replayButton.isEnabled = canReplay
        openButton.isEnabled = latestBundlePath != nil || true
    }

    private func descriptionForState() -> String {
        switch state {
        case "ready":
            if controller == "replay" { return "Verified and ready to replay the recorded controller input." }
            return controller == "connected" && discAccepted ? "Verified and ready. Start Capture will launch the ordinary boot." : "The environment reported ready, but its prerequisites are incomplete."
        case "controller_required": return "Connect a physical controller and configure the isolated Dolphin profile before capturing."
        case "verifying": return "Checking the owned disc, controller profile and pinned Dolphin environment."
        case "starting": return "Launching the ordinary reference boot."
        case "running", "recording": return "Recording source frames and controller events."
        case "replay", "replaying": return "Replaying the selected finalized capture."
        case "replay_matched": return "Dolphin reproduced the recorded observations through teardown."
        case "replay_diverged": return "Dolphin finished; the comparison report identifies a difference."
        case "replay_comparison_failed": return "The replay recording is preserved. Its comparison report needs another attempt."
        case "finalizing": return "Writing and validating the capture bundle."
        case "accepted": return "The capture bundle passed its completeness checks."
        case "incomplete": return "The capture stopped before a complete bundle was produced."
        case "failed": return "The environment reported a failure; see the detail below."
        default: return "Waiting for an explicit status."
        }
    }

    private func display(_ raw: String) -> String {
        raw.replacingOccurrences(of: "_", with: " ").capitalized
    }

    private func capturesRootURL() -> URL? {
        guard let appSupport = try? FileManager.default.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: false
        ) else { return nil }
        return appSupport.appendingPathComponent(
            "WebMelee Reference Capture/Captures", isDirectory: true
        )
    }
}
