// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "TitanCore",
    platforms: [
        .macOS(.v14),
        .iOS(.v17),
    ],
    products: [
        .library(name: "TitanCore", targets: ["TitanCore"]),
    ],
    targets: [
        .target(
            name: "TitanCore",
            path: "Titan",
            sources: [
                "Net/TelnetParser.swift",
                "Net/McpParser.swift",
                "Model/AppSettings.swift",
                "Model/Condition.swift",
                "Model/Hook.swift",
                "Model/Spawn.swift",
                "Model/TimerEngine.swift",
                "Model/Trigger.swift",
                "Model/Variables.swift",
            ]
        ),
        .testTarget(
            name: "TitanCoreTests",
            dependencies: ["TitanCore"],
            path: "Tests/TitanCoreTests"
        ),
    ]
)
