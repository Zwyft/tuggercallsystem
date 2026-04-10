use esp_idf_hal::gpio::*;
use esp_idf_hal::peripherals::Peripherals;
use esp_idf_hal::spi::*;

pub struct Board {
    pub spi_bus: SpiDriver<'static>,
    // Radio Pins
    pub lora_nss: PinDriver<'static, Gpio8, Output>,
    pub lora_rst: PinDriver<'static, Gpio12, Output>,
    pub lora_busy: PinDriver<'static, Gpio13, Input>,
    pub lora_dio1: PinDriver<'static, Gpio14, Input>,
    // Display Pins (Heltec Wireless Paper V1.1)
    pub display_cs: PinDriver<'static, Gpio4, Output>,
    pub display_dc: PinDriver<'static, Gpio5, Output>,
    pub display_rst: PinDriver<'static, Gpio6, Output>,
    pub display_busy: PinDriver<'static, Gpio7, Input>,
    // Buttons
    pub btn_select: PinDriver<'static, Gpio0, Input>,
    // CONFLICT: User defined UP=12, DOWN=13. But these are Radio RST/BUSY.
    // Commenting out to prevent runtime resource claiming error.
    // pub btn_up: PinDriver<'static, Gpio12, Input>,
    // pub btn_down: PinDriver<'static, Gpio13, Input>,
}

pub fn init() -> anyhow::Result<Board> {
    let peripherals = Peripherals::take()?;
    let pins = peripherals.pins;

    let sclk = pins.gpio9;
    let mosi = pins.gpio10;
    let miso = pins.gpio11;

    // Initialize SPI Driver (Shared Bus)
    let config = config::DriverConfig::default();
    let spi_bus = SpiDriver::new(peripherals.spi2, sclk, mosi, Some(miso), &config)?;

    // Radio
    let lora_nss = PinDriver::output(pins.gpio8)?;
    let lora_rst = PinDriver::output(pins.gpio12)?;
    let lora_busy = PinDriver::input(pins.gpio13)?;
    let lora_dio1 = PinDriver::input(pins.gpio14)?;

    // Display
    let display_cs = PinDriver::output(pins.gpio4)?;
    let display_dc = PinDriver::output(pins.gpio5)?;
    let display_rst = PinDriver::output(pins.gpio6)?;
    let display_busy = PinDriver::input(pins.gpio7)?;

    // Buttons
    let btn_select = PinDriver::input(pins.gpio0)?;

    Ok(Board {
        spi_bus,
        lora_nss,
        lora_rst,
        lora_busy,
        lora_dio1,
        display_cs,
        display_dc,
        display_rst,
        display_busy,
        btn_select,
    })
}
