// @generated by autocargo

use std::env;
use std::fs;
use std::path::Path;
use thrift_compiler::Config;
use thrift_compiler::GenContext;
const CRATEMAP: &str = "\
blame crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
bonsai crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
bssm crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
changeset_info crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
content crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
data crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
deleted_manifest crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
fastlog crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
fsnodes crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
id crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
path crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
raw_bundle2 crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
redaction crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
sharded_map crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
skeleton_manifest crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
test_manifest crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
time crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
unodes crate //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
";
#[rustfmt::skip]
fn main() {
    println!("cargo:rerun-if-changed=thrift_build.rs");
    let out_dir = env::var_os("OUT_DIR").expect("OUT_DIR env not provided");
    let cratemap_path = Path::new(&out_dir).join("cratemap");
    fs::write(cratemap_path, CRATEMAP).expect("Failed to write cratemap");
    let mut conf = Config::from_env(GenContext::Types)
        .expect("Failed to instantiate thrift_compiler::Config");
    let cargo_manifest_dir = env::var_os("CARGO_MANIFEST_DIR")
        .expect("CARGO_MANIFEST_DIR not provided");
    let mut base_path = Path::new(&cargo_manifest_dir)
        .join("../../../..")
        .canonicalize()
        .expect("Failed to canonicalize base_path");
    if cfg!(windows) {
        base_path = base_path.to_string_lossy().trim_start_matches(r"\\?\").into();
    }
    conf.base_path(base_path);
    conf.types_crate("mononoke_types_serialization__types");
    conf.clients_crate("mononoke_types_serialization__clients");
    conf.services_crate("mononoke_types_serialization__services");
    let srcs = &[
        "blame.thrift",
        "bonsai.thrift",
        "bssm.thrift",
        "changeset_info.thrift",
        "content.thrift",
        "data.thrift",
        "deleted_manifest.thrift",
        "fastlog.thrift",
        "fsnodes.thrift",
        "id.thrift",
        "path.thrift",
        "raw_bundle2.thrift",
        "redaction.thrift",
        "sharded_map.thrift",
        "skeleton_manifest.thrift",
        "test_manifest.thrift",
        "time.thrift",
        "unodes.thrift",
    ];
    conf.run(srcs).expect("Failed while running thrift compilation");
}
