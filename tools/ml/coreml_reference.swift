// Runs every .mlmodelc in a folder through Core ML on deterministic inputs and writes the outputs, so the ONNX
// conversions (tools/ml/mil2onnx.py, espresso2onnx.py) can be compared against what Apple's runtime really computes.
//
//   swiftc -O coreml_reference.swift -o coreml_reference
//   ./coreml_reference <folder with *.mlmodelc> <out folder>
//   (copy <out folder> back, then: python compare_with_coreml.py <out folder> <onnx folder>)
//
// Input value i of input k: h = (i * 2654435761 ^ (k + 1) * 97) mod 2^32; v = (h / 2^32 - 0.5) * 0.6   (same code in python).
import CoreML
import Foundation

func fill(_ shape: [Int], dtype: MLMultiArrayDataType, k: Int) throws -> MLMultiArray {
    let a = try MLMultiArray(shape: shape.map { NSNumber(value: $0) }, dataType: dtype)
    let n = shape.reduce(1, *)
    for i in 0..<n {
        let h = (UInt32(truncatingIfNeeded: i) &* 2654435761) ^ (UInt32(k + 1) &* 97)
        a[i] = NSNumber(value: (Double(h) / 4294967296.0 - 0.5) * 0.6)
    }
    return a
}

let args = CommandLine.arguments
guard args.count == 3 else { print("usage: coreml_reference <models dir> <out dir>"); exit(1) }
let fm = FileManager.default
try fm.createDirectory(atPath: args[2], withIntermediateDirectories: true)
var manifest: [[String: Any]] = []
for name in try fm.contentsOfDirectory(atPath: args[1]).sorted() where name.hasSuffix(".mlmodelc") {
    let url = URL(fileURLWithPath: args[1]).appendingPathComponent(name)
    do {
        let cfg = MLModelConfiguration()
        cfg.computeUnits = .cpuOnly            // closest to a plain fp32 reference
        let model = try MLModel(contentsOf: url, configuration: cfg)
        var feats: [String: MLFeatureValue] = [:]
        var inputs: [[String: Any]] = []
        for (k, (iname, desc)) in model.modelDescription.inputDescriptionsByName.sorted(by: { $0.key < $1.key }).enumerated() {
            guard let c = desc.multiArrayConstraint else { continue }
            let shape = c.shape.map { $0.intValue }
            feats[iname] = MLFeatureValue(multiArray: try fill(shape, dtype: c.dataType, k: k))
            inputs.append(["name": iname, "shape": shape, "k": k])
        }
        let out = try model.prediction(from: try MLDictionaryFeatureProvider(dictionary: feats))
        var outputs: [[String: Any]] = []
        for oname in out.featureNames.sorted() {
            guard let m = out.featureValue(for: oname)?.multiArrayValue else { continue }
            let n = m.count
            var data = Data(count: n * 4)
            data.withUnsafeMutableBytes { (p: UnsafeMutableRawBufferPointer) in
                let f = p.bindMemory(to: Float.self)
                for i in 0..<n { f[i] = m[i].floatValue }
            }
            let file = "\(name).\(oname).f32"
            try data.write(to: URL(fileURLWithPath: args[2]).appendingPathComponent(file))
            outputs.append(["name": oname, "shape": m.shape.map { $0.intValue }, "file": file])
        }
        manifest.append(["model": name, "inputs": inputs, "outputs": outputs])
        print("ok", name)
    } catch {
        manifest.append(["model": name, "error": "\(error)"])
        print("FAILED", name, error)
    }
}
let json = try JSONSerialization.data(withJSONObject: manifest, options: [.prettyPrinted])
try json.write(to: URL(fileURLWithPath: args[2]).appendingPathComponent("manifest.json"))
