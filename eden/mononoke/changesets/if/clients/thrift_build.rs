// @generated by autocargo

use std::env;
use std::fs;
use std::path::Path;
use thrift_compiler::Config;
use thrift_compiler::GenContext;
const CRATEMAP: &str = "\
blame mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
bonsai mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
bssm mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
changeset_entry crate //eden/mononoke/changesets/if:changeset-entry-thrift-rust
changeset_info mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
content mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
data mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
deleted_manifest mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
fastlog mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
fsnodes mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
id mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
path mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
raw_bundle2 mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
redaction mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
sharded_map mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
skeleton_manifest mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
test_manifest mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
time mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
unodes mononoke_types_serialization //eden/mononoke/mononoke_types/serialization:mononoke_types_serialization-rust
";
#[rustfmt::skip]
fn main() {
    println!("cargo:rerun-if-changed=thrift_build.rs");
    let out_dir = env::var_os("OUT_DIR").expect("OUT_DIR env not provided");
    let cratemap_path = Path::new(&out_dir).join("cratemap");
    fs::write(cratemap_path, CRATEMAP).expect("Failed to write cratemap");
    let mut conf = Config::from_env(GenContext::Clients)
        .expect("Failed to instantiate thrift_compiler::Config");
    let cargo_manifest_dir = env::var_os("CARGO_MANIFEST_DIR")
        .expect("CARGO_MANIFEST_DIR not provided");
    let mut base_path = Path::new(&cargo_manifest_dir)
        .join("../../../../..")
        .canonicalize()
        .expect("Failed to canonicalize base_path");
    if cfg!(windows) {
        base_path = base_path.to_string_lossy().trim_start_matches(r"\\?\").into();
    }
    conf.base_path(base_path);
    conf.types_crate("changeset-entry-thrift__types");
    conf.clients_crate("changeset-entry-thrift__clients");
    conf.services_crate("changeset-entry-thrift__services");
    let srcs = &["../changeset_entry.thrift"];
    conf.run(srcs).expect("Failed while running thrift compilation");
}
