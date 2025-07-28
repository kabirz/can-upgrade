use std::env;
use std::path::PathBuf;

fn main() {
    slint_build::compile("ui/app-window.slint").expect("Slint build failed");
    if std::env::var("TARGET").unwrap() == "x86_64-pc-windows-msvc" {
        println!("cargo:rerun-if-changed=PCANBasic/PCANBasic.h");
        println!("cargo:rustc-link-search=PCANBasic");
        println!("cargo:rustc-link-lib=PCANBasic");

        let bindings = bindgen::Builder::default()
            .header("PCANBasic/PCANBasic.h")
            .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
            .generate()
            .expect("Unable to generate bindings");

        let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
        bindings.write_to_file(out_path.join("bindings.rs"))
            .expect("Couldn't write bindings!");
    }
}
