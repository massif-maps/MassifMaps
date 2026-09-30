import UIKit

/// Runs each check, logs it and shows the lines over the app's view.
class Checks {
    private(set) var lines: [String] = []

    func run(_ name: String, _ block: () throws -> String) {
        let line: String
        do {
            line = "ok    \(name) \(try block())"
        } catch {
            line = "FAIL  \(name): \(error)"
        }
        NSLog("massif-release-check %@", line)
        lines.append(line)
    }

    func show(in view: UIView) {
        let status = UILabel()
        status.numberOfLines = 0
        status.backgroundColor = UIColor(white: 1, alpha: 0.9)
        status.text = lines.joined(separator: "\n")
        status.frame = CGRect(x: 8, y: 60, width: view.bounds.width - 16, height: 20 * CGFloat(lines.count + 1))
        view.addSubview(status)
    }
}

func launch(_ root: UIViewController) -> UIWindow {
    let window = UIWindow(frame: UIScreen.main.bounds)
    window.rootViewController = root
    window.makeKeyAndVisible()
    return window
}
