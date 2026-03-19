import Foundation

// MARK: - App Settings

class AppSettings {
    private let defaults = UserDefaults.standard

    var fontSize: Int {
        get { defaults.object(forKey: "fontSize") as? Int ?? 14 }
        set { defaults.set(newValue, forKey: "fontSize") }
    }

    var fontSizeLandscape: Int {
        get { defaults.object(forKey: "fontSizeLandscape") as? Int ?? 12 }
        set { defaults.set(newValue, forKey: "fontSizeLandscape") }
    }

    var scrollbackLines: Int {
        get { defaults.object(forKey: "scrollbackLines") as? Int ?? 20000 }
        set { defaults.set(newValue, forKey: "scrollbackLines") }
    }

    var defaultPort: Int {
        get { defaults.object(forKey: "defaultPort") as? Int ?? 4201 }
        set { defaults.set(newValue, forKey: "defaultPort") }
    }

    var defaultSsl: Bool {
        get { defaults.bool(forKey: "defaultSsl") }
        set { defaults.set(newValue, forKey: "defaultSsl") }
    }

    var keepScreenOn: Bool {
        get { defaults.bool(forKey: "keepScreenOn") }
        set { defaults.set(newValue, forKey: "keepScreenOn") }
    }
}
