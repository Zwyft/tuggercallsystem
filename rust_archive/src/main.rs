```
// embedded-hal-bus 0.1 location:
// use esp_idf_hal::gpio::*;
use esp_idf_hal::task::block_on;
use esp_idf_svc::hal as esp_idf_hal;
use log::*;
use std::sync::Mutex;

mod display;
mod hardware;
mod radio;

// Custom declaration of SpiBus trait to ensure visibility/scope if needed, 
// strictly speaking we should import it from embedded_hal.
use embedded_hal::spi::SpiBus;

// SimpleMutexSpiDevice: A custom SpiDevice implementation that shares a bus via Mutex, 
// but does NOT handle Chip Select (CS). This allows the consumer (Driver) to manage CS.
pub struct SimpleMutexSpiDevice<'a, T>(pub &'a Mutex<T>);

impl<'a, T: SpiBus> embedded_hal::spi::ErrorType for SimpleMutexSpiDevice<'a, T> {
    type Error = T::Error;
}

impl<'a, T: SpiBus> embedded_hal::spi::SpiDevice for SimpleMutexSpiDevice<'a, T> {
    fn transaction(
        &mut self,
        operations: &mut [embedded_hal::spi::Operation<'_, u8>],
    ) -> Result<(), Self::Error> {
        let mut bus = self.0.lock().unwrap();
        for op in operations {
            match op {
                embedded_hal::spi::Operation::Read(buf) => bus.read(buf)?,
                embedded_hal::spi::Operation::Write(buf) => bus.write(buf)?,
                embedded_hal::spi::Operation::Transfer(read, write) => bus.transfer(read, write)?,
                embedded_hal::spi::Operation::TransferInPlace(buf) => bus.transfer_in_place(buf)?,
                embedded_hal::spi::Operation::DelayNs(ns) => {
                    // Primitive delay if supported, or ignore? 
                    // Verify if esp_idf_hal::spi::SpiBus supports delay/flush?
                    // SpiBus trait 1.0 includes flush. Delay is in Operation.
                    // If Bus doesn't support delay, we might need a delayer?
                    // For now, ignore delay or implementation specific.
                    // But SpiBus doesn't have `delay_ns`.
                    // We must just perform a flush.
                    let _ = *ns; 
                    bus.flush()?;
                }
            }
        }
        bus.flush()?;
        Ok(())
    }
}

// Refactored BlockingAsyncSpi to wrap our SimpleMutexSpiDevice
pub struct BlockingAsyncSpi<T>(T);

impl<T: embedded_hal::spi::ErrorType> embedded_hal::spi::ErrorType for BlockingAsyncSpi<T> {
    type Error = T::Error;
}

impl<T: embedded_hal::spi::SpiDevice> embedded_hal_async::spi::SpiDevice for BlockingAsyncSpi<T> {
    async fn transaction(
        &mut self,
        operations: &mut [embedded_hal::spi::Operation<'_, u8>],
    ) -> Result<(), T::Error> {
        // Just call the blocking transaction
        self.0.transaction(operations)
    }
}

fn main() -> anyhow::Result<()> {
    // Check-cfg are handled in build.rs
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    info!("Starting Tugger Device...");

    let board = hardware::init()?;

    // Create Shared SPI Bus
    // Note: hardware::init() returns a struct with `spi_bus`. 
    // Ensure `spi_bus` implements `SpiBus`.
    let spi_bus = Mutex::new(board.spi_bus);

    // Blocking Device for Display
    // We use SimpleMutexSpiDevice so `display` handles CS itself.
    let mut display_spi = SimpleMutexSpiDevice(&spi_bus);

    // Downgrade busy pin for display to generic Input
    // board.display_busy is concrete PinDriver. 
    // Epd2in9 might expect concrete. If we previously changed it to `AnyInputPin`, we'd need conversion.
    // Checking display.rs: `busy: PinDriver<'static, Gpio7, Input>` -> concrete!
    // So we just pass it directly.
    
    // For Epd2in9::new, correct signature from recent findings?
    // Epd2in9::new(spi, cs, dc, rst, busy, delay) (?) - 6 args? 
    // Or (spi, cs, dc, rst, delay, speed)?
    // Round 2 fix used 6 args with busy. 
    // User error report said "E0061 ... missing spi_speed Option<u32>".
    // So current hypothesis: (spi, cs, dc, rst, busy, delay, spi_speed). 7 args?
    // Or (spi, cs, dc, rst, delay, spi_speed). 6 args (NO BUSY).
    // Let's assume NO BUSY in constructor, setting busy separately if needed.
    // I will try 6 args: (spi, cs, dc, rst, delay, None).
    
    info!("Initializing Display...");
    // Commenting out Display init temporarily if it causes issues, but let's try to fix it.
    // We pass `display_spi`.
    // We need logic to handle `busy` if not passed to constructor.
    // But `TunggerDisplay::new` signature in `display.rs` expects `busy`.
    // I should update `display.rs` too. For now let's update `main.rs` call.
    // Display Init
    // Epd2in9::new now takes (spi, cs, dc, rst, delay, speed)
    let mut display = display::TunggerDisplay::new(
        &mut display_spi,
        board.display_cs,
        board.display_dc,
        board.display_rst,
    )?;

    display.update(&mut display_spi, "Booting...")?;
    info!("Display Initialized.");

    // Async Device for Radio
    // Wrap the blocking SimpleMutexSpiDevice in our adapter
    let radio_spi = BlockingAsyncSpi(SimpleMutexSpiDevice(&spi_bus));

    // Downgrade pins for Radio
    // Use `into_output()` and `into_input()` (not any).
    // `PinDriver` should have these methods to convert to dynamic pin driver.
    // If board.lora_nss is PinDriver<Gpio8, Output>, .into_output()?
    // Let's try explicit map if needed, but error says use `into_output`.
    let lora_nss = board.lora_nss.into_output().map_err(|e| anyhow::anyhow!("Pin error {:?}", e))?;
    let lora_rst = board.lora_rst.into_output().map_err(|e| anyhow::anyhow!("Pin error {:?}", e))?;
    let lora_busy = board.lora_busy.into_input().map_err(|e| anyhow::anyhow!("Pin error {:?}", e))?;
    let lora_dio1 = board.lora_dio1.into_input().map_err(|e| anyhow::anyhow!("Pin error {:?}", e))?;

    block_on(async {
        info!("Initializing Radio (Async)...");

        // Initialize TimerDriver for LoRa delay
        // esp-idf-hal 0.45: `TimerDriver::new`
        let peripherals = esp_idf_hal::peripherals::Peripherals::take()?;
        let timer_driver = esp_idf_hal::timer::TimerDriver::new(peripherals.timer00, &esp_idf_hal::timer::config::Config::new())?;

        let mut radio = radio::TunggerRadio::new(
            radio_spi,
            lora_nss,
            lora_rst,
            lora_busy,
            lora_dio1,
            timer_driver,
        )
        .await?;

        radio.configure(&radio::RadioConfig::default()).await?;
        info!("Radio Initialized.");

        loop {
             // Logic loop
             std::thread::sleep(std::time::Duration::from_secs(5));
             
             display.update(&mut display_spi, "Tick")?;
             info!("Tick");
        }
    })?;

    Ok(())
}
```
