import UIKit

/// A map SDK product on its own (MassifMaps, the full profile, unless generate.sh names another).
@main
class AppDelegate: UIResponder, UIApplicationDelegate {
    var window: UIWindow?

    func application(_ application: UIApplication, didFinishLaunchingWithOptions options: [UIApplication.LaunchOptionsKey: Any]? = nil) -> Bool {
        window = launch(MapCheckController())
        return true
    }
}

class MapCheckController: UIViewController {
    private let checks = Checks()

    override func loadView() {
        view = mapCheckView(checks)
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        checks.show(in: view)
    }
}
