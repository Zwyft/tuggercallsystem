fn main() {
    embuild::espidf::sysenv::relay();
    embuild::espidf::sysenv::output(); // Only necessary for building the examples

    // Silence rustc >= 1.80 unexpected_cfgs warnings
    println!("cargo:rustc-check-cfg=cfg(esp32)");
    println!("cargo:rustc-check-cfg=cfg(esp32s2)");
    println!("cargo:rustc-check-cfg=cfg(esp32s3)");
    println!("cargo:rustc-check-cfg=cfg(esp32c3)");
    println!("cargo:rustc-check-cfg=cfg(esp32h2)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_bt_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_bt_bluedroid_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_esp_wifi_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_esp_event_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_esp_eth_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_mdns_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_espressif__mdns_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_mqtt_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_lwip_ipv4_napt)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_esp_netif_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_nvs_flash_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_app_update_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_spi_flash_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_esp_timer_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_vfs_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_esp_http_client_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_esp_http_server_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_esp_https_server_enable)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_httpd_ws_support)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_esp_event_post_from_isr)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_esp_tls_psk_verification)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_log_colors)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_eth_use_esp32_emac)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_eth_spi_ethernet_dm9051)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_eth_spi_ethernet_w5500)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_eth_spi_ethernet_ksz8851snl)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_eth_use_openeth)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_version)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_version_major, values(\"4\", \"5\"))");
    println!(
        "cargo:rustc-check-cfg=cfg(esp_idf_version_minor, values(\"0\", \"1\", \"3\", \"4\"))"
    );
    println!(
        "cargo:rustc-check-cfg=cfg(esp_idf_version_patch, values(\"0\", \"1\", \"2\", \"3\"))"
    );
    println!("cargo:rustc-check-cfg=cfg(esp_idf_version_full)");

    println!("cargo:rustc-check-cfg=cfg(esp_idf_lwip_ppp_support)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_lwip_slip_support)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_esp_timer_supports_isr_dispatch_method)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_esp_tls_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_esp_tls_using_mbedtls)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_esp_tls_using_wolfssl)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_mbedtls_certificate_bundle)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_lwip_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_tcp_transport_enabled)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_ws_transport)");
    println!("cargo:rustc-check-cfg=cfg(esp_idf_comp_espressif__esp_websocket_client_enabled)");
}
