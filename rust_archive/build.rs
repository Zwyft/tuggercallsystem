fn main() {
    println!("cargo::rustc-check-cfg=cfg(esp_idf_version, values(\"4.3\"))");
    println!(
        "cargo::rustc-check-cfg=cfg(esp_idf_version_full, values(\"5.1.0\", \"5.1.1\", \"5.1.2\"))"
    );
    embuild::espidf::sysenv::output();
}
