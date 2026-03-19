import Foundation

// MARK: - Condition Context

struct ConditionContext {
    var isConnected: Bool = false
    var idleSeconds: Int = 0
}

// MARK: - Trigger Condition

enum TriggerCondition: Codable {
    case stringMatch(pattern: String, negate: Bool = false)
    case worldConnected(negate: Bool = false)
    case worldIdle(seconds: Int, negate: Bool = false)
    case group(conditions: [TriggerCondition], anded: Bool = true)
    case negated(condition: Box<TriggerCondition>)

    func evaluate(line: String, context: ConditionContext) -> Bool {
        switch self {
        case .stringMatch(let pattern, let negate):
            let matches = (try? Regex(pattern).ignoresCase().firstMatch(in: line)) != nil
            return negate ? !matches : matches

        case .worldConnected(let negate):
            return negate ? !context.isConnected : context.isConnected

        case .worldIdle(let seconds, let negate):
            let idle = context.idleSeconds >= seconds
            return negate ? !idle : idle

        case .group(let conditions, let anded):
            if anded {
                return conditions.allSatisfy { $0.evaluate(line: line, context: context) }
            } else {
                return conditions.contains { $0.evaluate(line: line, context: context) }
            }

        case .negated(let box):
            return !box.value.evaluate(line: line, context: context)
        }
    }
}

// Box wrapper for recursive enum Codable conformance
struct Box<T: Codable>: Codable {
    let value: T
}
