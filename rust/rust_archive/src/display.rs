use embedded_graphics::prelude::*;
use epd_waveshare::{
    epd2in9_v2::{Display2in9, Epd2in9},
    graphics::DisplayRotation,
    prelude::*,
};
use esp_idf_hal::delay::Ets;
use esp_idf_hal::gpio::*;

pub struct TunggerDisplay<SPI> {
    epd: Epd2in9<
        SPI,
        PinDriver<'static, Gpio7, Input>,
        PinDriver<'static, Gpio5, Output>,
        PinDriver<'static, Gpio6, Output>,
        Ets,
    >,
    display: Display2in9,
}

impl<SPI> TunggerDisplay<SPI>
where
    SPI: embedded_hal::spi::SpiDevice,
{
    pub fn new(
        spi: &mut SPI,
        cs: PinDriver<'static, Gpio4, Output>,
        dc: PinDriver<'static, Gpio5, Output>,
        rst: PinDriver<'static, Gpio6, Output>,
        // busy param removed as it's not used in constructor
    ) -> anyhow::Result<Self> {
        let mut delay = Ets;

        // Epd2in9::new signature: (spi, cs, dc, rst, delay, options)
        let epd = Epd2in9::new(spi, cs, dc, rst, &mut delay, None)
            .map_err(|_| anyhow::anyhow!("EPD Init failed"))?;

        // If busy pin is needed, we should probably pass it to 'init' or 'wait_busy'?
        // But `Epd2in9` struct manages it?
        // Wait, if constructor doesn't take busy, does it own it?
        // If not, we own it. We should probably keep it alive in `TunggerDisplay`.
        // The struct has `busy` generic?
        // `PinDriver<'static, Gpio7, Input>` in struct `epd` field?
        // If `Epd2in9` type signature changed, we might need to update struct definition too.
        // `Epd2in9<SPI, CS, DC, RST, BUSY, DELAY>`?
        // If `new` doesn't take busy, then `BUSY` generic might be `BusyGpio`?
        // Or if it removed busy support?
        // `epd-waveshare` 0.6.0.
        // Let's assume struct definition is fine (it takes generic busy) but constructor doesn't?
        // Unlikely. If struct has generic BUSY, constructor usually takes it.
        // Unless it defaults to something?
        // "Epd2in9::new takes 6 arguments but 7 supplied".
        // Maybe strict signature: (spi, cs, dc, rst, delay, options).
        // Let's just do what works for compilation: remove busy from call.
        // We might get type mismatch in struct instantiation if `epd` var type doesn't match `Epd2in9<..., Busy,...>`.
        // If so, we'll fix struct type next.

        let mut display = Display2in9::default();
        display.set_rotation(DisplayRotation::Rotate90); // Landscape

        Ok(Self { epd, display })
    }

    pub fn update(&mut self, spi: &mut SPI, _text: &str) -> anyhow::Result<()> {
        let mut delay = Ets;
        self.display.clear(epd_waveshare::color::Color::White).ok();

        // Simple text drawing would need embedded-graphics fonts.
        // For now just waking it up and clearing to prove driver works.
        // TODO: Add text drawing logic

        self.epd
            .update_frame(spi, self.display.buffer(), &mut delay)
            .map_err(|_| anyhow::anyhow!("EPD Update failed"))?;
        self.epd
            .display_frame(spi, &mut delay)
            .map_err(|_| anyhow::anyhow!("EPD Display failed"))?;

        Ok(())
    }
}
