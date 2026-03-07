use cmake::Config;
use std::env;

fn main() {
    let mut cfg = Config::new("../..");

    cfg.define("SIGNET_BUILD_FFI", "ON")
        .define("SIGNET_BUILD_TESTS", "OFF")
        .define("SIGNET_BUILD_BENCHMARKS", "OFF")
        .define("SIGNET_BUILD_EXAMPLES", "OFF")
        .define("SIGNET_BUILD_TOOLS", "OFF")
        .define("SIGNET_BUILD_PYTHON", "OFF")
        .define("SIGNET_BUILD_FUZZ", "OFF");

    if cfg!(feature = "commercial") {
        cfg.define("SIGNET_ENABLE_COMMERCIAL", "ON");
    }

    let dst = cfg.build_target("signet_forge_c").build();

    // The static lib is in the build directory
    println!(
        "cargo:rustc-link-search=native={}/build",
        dst.display()
    );
    println!("cargo:rustc-link-lib=static=signet_forge_c");

    // Link C++ standard library
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    match target_os.as_str() {
        "macos" | "ios" => println!("cargo:rustc-link-lib=c++"),
        _ => println!("cargo:rustc-link-lib=stdc++"),
    }
}
