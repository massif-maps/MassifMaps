import UIKit
import ValhallaRouting

/// The standalone routing product beside a map without routing (MassifMapsCore): the pairing it is
/// for, since the full profile defines the same routing classes.
@main
class AppDelegate: UIResponder, UIApplicationDelegate {
    var window: UIWindow?

    func application(_ application: UIApplication, didFinishLaunchingWithOptions options: [UIApplication.LaunchOptionsKey: Any]? = nil) -> Bool {
        window = launch(RoutingCheckController())
        return true
    }
}

class RoutingCheckController: UIViewController {
    private let checks = Checks()

    override func loadView() {
        view = mapCheckView(checks)
        checks.run("routing library") {
            let routing = MSFValhallaRoutingService(mbTilesPaths: [])
            return "profile \(routing.profile)"
        }
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        checks.show(in: view)
    }
}
