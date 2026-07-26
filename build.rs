use std::fs;
use std::path::Path;

fn main() {
    let native_dir = Path::new("native");
    if !native_dir.exists() {
        panic!("native directory not found!");
    }

    let mut build = cc::Build::new();
    build.cpp(true)
         .opt_level(3)
         .flag("-std=c++17");

    if let Ok(entries) = fs::read_dir(native_dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if let Some(ext) = path.extension().and_then(|e| e.to_str()) {
                println!("cargo:rerun-if-changed={}", path.display());
                
                match ext {
                    "cpp" => {
                        build.file(&path);
                    }
                    "c" => {
                        cc::Build::new()
                            .file(&path)
                            .compile(&format!("c_{}", path.file_stem().unwrap().to_str().unwrap()));
                    }
                    "m" => {
                        cc::Build::new()
                            .flag("-x")
                            .flag("objective-c")
                            .flag("-fno-objc-arc")
                            .flag("-framework")
                            .flag("Cocoa")
                            .flag("-framework")
                            .flag("Metal")
                            .flag("-framework")
                            .flag("QuartzCore")
                            .flag("-framework")
                            .flag("IOKit")
                            .flag("-framework")
                            .flag("IOSurface")
                            .file(&path)
                            .compile(&format!("objc_{}", path.file_stem().unwrap().to_str().unwrap()));
                    }
                    _ => {}
                }
            }
        }
    }

    build.compile("native_core");

    println!("cargo:rustc-link-lib=framework=Cocoa");
    println!("cargo:rustc-link-lib=framework=Metal");
    println!("cargo:rustc-link-lib=framework=QuartzCore");
    println!("cargo:rustc-link-lib=framework=IOKit");
    println!("cargo:rustc-link-lib=framework=IOSurface");

    println!("cargo:rerun-if-changed=build.rs");
}
