// @generated by autocargo

use std::env;
use std::fs;
use std::path::Path;
use thrift_compiler::Config;
use thrift_compiler::GenContext;
const CRATEMAP: &str = "\
hg_mutation_entry crate //eden/mononoke/mercurial/mutation/if:hg_mutation_entry_thrift-rust
mercurial_thrift mercurial_thrift //eden/mononoke/mercurial/types/if:mercurial-thrift-rust
";
#[rustfmt::skip]
fn main() {
    println!("cargo:rerun-if-changed=thrift_build.rs");
    let out_dir = env::var_os("OUT_DIR").expect("OUT_DIR env not provided");
    let cratemap_path = Path::new(&out_dir).join("cratemap");
    fs::write(cratemap_path, CRATEMAP).expect("Failed to write cratemap");
    let mut conf = Config::from_env(GenContext::Mocks)
        .expect("Failed to instantiate thrift_compiler::Config");
    let cargo_manifest_dir = env::var_os("CARGO_MANIFEST_DIR")
        .expect("CARGO_MANIFEST_DIR not provided");
    let mut base_path = Path::new(&cargo_manifest_dir)
        .join("../../../../../..")
        .canonicalize()
        .expect("Failed to canonicalize base_path");
    if cfg!(windows) {
        base_path = base_path.to_string_lossy().trim_start_matches(r"\\?\").into();
    }
    conf.base_path(base_path);
    conf.types_crate("hg_mutation_entry_thrift__types");
    conf.clients_crate("hg_mutation_entry_thrift__clients");
    conf.services_crate("hg_mutation_entry_thrift__services");
    let srcs = &["../hg_mutation_entry.thrift"];
    conf.run(srcs).expect("Failed while running thrift compilation");
}
