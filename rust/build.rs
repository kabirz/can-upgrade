
fn main() {
    slint_build::compile("ui/app-window.slint").expect("Slint build failed");
    if std::env::var("TARGET").unwrap() == "x86_64-pc-windows-msvc" {
        println!("cargo:rustc-link-search=PCANBasic");
        println!("cargo:rustc-link-lib=PCANBasic");
    }
#[cfg(target_os = "windows")]
    {
        let mut res = winres::WindowsResource::new();
        res.set_icon("ui/icon.ico")
            .set("ProductName", "My Rust App")
            .set("FileDescription", "My Awesome Rust Application");
        res.compile().unwrap();
    }
}
