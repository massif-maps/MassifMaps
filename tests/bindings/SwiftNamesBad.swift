/* A name that does not exist. This file MUST NOT type-check - see run.sh. */
func bad() -> String {
    let nothing: MassifProperty = .noSuchPropertyExists
    return nothing.rawValue
}
