// use esp_idf_hal::delay::Ets;
use esp_idf_hal::gpio::*;
use esp_idf_hal::timer::TimerDriver;
use lora_phy::iv::GenericSx126xInterfaceVariant;
use lora_phy::sx126x::{self, Sx1262, Sx126x};
use lora_phy::LoRa;

pub struct RadioConfig {
    pub frequency: u32,
    pub bandwidth: u32,
    pub spreading_factor: u8,
    pub coding_rate: u8,
    pub output_power: i8,
}

impl Default for RadioConfig {
    fn default() -> Self {
        Self {
            frequency: 915_000_000,
            bandwidth: 125_000,
            spreading_factor: 9,
            coding_rate: 7,
            output_power: 14,
        }
    }
}

// Sx126x<SPI, BoardType, Delay>? Or SPI, InterfaceVariant.
// lora-phy v3: Sx126x<SPI, IV, D>
// IV must be the Type of the interface variant struct, not the enum value.
// The type of `Sx126xVariant::Sx1262(...)` is `Sx126xVariant<PinDriver<...>, ...>`.
// BUT, Sx126xVariant is an enum.
// The error `expected type, found trait Sx126xVariant` suggests it might be a trait in v3 or I am using it wrong.
// Actually, in v3, `Sx126xVariant` is an enum. You cannot use an enum variant as a type parameter unless it's a const generic?
// NO. The second generic param of Sx126x is `IV`. IV must implement `InterfaceVariant`.
// Does the enum `Sx126xVariant` implement `InterfaceVariant`? YES.
// So `Sx126x<SPI, Sx126xVariant<'d, ...>, Ets>` SHOULD be correct if arguments match.
// BUT `Sx126x` does NOT take lifetime `'d`.
// My previous fix removed `'d` from `Sx126x`, but I kept it on `Sx126xVariant`.
// `Sx126xVariant` DOES take `'d` because it holds `PinDriver<'d>`.

// Let's rely on type inference for the struct field to avoid this mess.
// Use `Box<dyn RadioKind>`? No, overhead.
// Use `impl RadioKind`? Can't in struct field.
// We must name the type.

pub struct TunggerRadio<'d, SPI>
where
    SPI: embedded_hal_async::spi::SpiDevice,
{
    // Generics: <SPI, IV, Delay>
    // IV: GenericSx126xInterfaceVariant<CTRL, WAIT>
    // CTRL: PinDriver<'d, AnyOutputPin, Output>
    // WAIT: PinDriver<'d, AnyInputPin, Input>
    pub lora: LoRa<
        Sx126x<
            SPI,
            GenericSx126xInterfaceVariant<
                PinDriver<'d, AnyOutputPin, Output>,
                PinDriver<'d, AnyInputPin, Input>,
            >,
            Sx1262,
        >,
        TimerDriver<'d>,
    >,
}

impl<'d, SPI> TunggerRadio<'d, SPI>
where
    SPI: embedded_hal_async::spi::SpiDevice,
{
    pub async fn new(
        spi: SPI,
        // We accept concrete pins but downgrade them inside, or expect AnyPin?
        // Let's expect downgraded pins from main to keep signature simple here?
        // Or coerce here. Let's coerce here if possible, but PinDriver coercion consumes.
        // Better to ask caller to downgrade to avoid generic explosion.
        nss: PinDriver<'d, AnyOutputPin, Output>,
        rst: PinDriver<'d, AnyOutputPin, Output>,
        busy: PinDriver<'d, AnyInputPin, Input>,
        dio1: PinDriver<'d, AnyInputPin, Input>,
        delay: TimerDriver<'d>,
    ) -> anyhow::Result<Self> {
        let config = sx126x::Config {
            chip: Sx1262,
            tcxo_ctrl: Some(sx126x::TcxoCtrlVoltage::Ctrl1V7),
            use_dcdc: true,
            rx_boost: false,
        };

        // Note: GenericSx126xInterfaceVariant::new signature:
        // new(nss, reset, dio1, busy, ant_sw)
        let iv = GenericSx126xInterfaceVariant::new(nss, rst, dio1, busy, None)
            .map_err(|e| anyhow::anyhow!("IV init failed: {:?}", e))?;

        // Construct Sx1262 directly
        let radio_kind = Sx126x::new(spi, iv, config);

        let lora = LoRa::new(radio_kind, true, delay)
            .await
            .map_err(|e| anyhow::anyhow!("LoRa init failed: {:?}", e))?;

        Ok(Self { lora })
    }

    pub async fn configure(&mut self, cfg: &RadioConfig) -> anyhow::Result<()> {
        let mdltn_params = self
            .lora
            .create_modulation_params(
                lora_phy::mod_params::SpreadingFactor::_9,
                lora_phy::mod_params::Bandwidth::_125KHz, // Fixed Case
                lora_phy::mod_params::CodingRate::_4_7,
                cfg.frequency,
            )
            .map_err(|e| anyhow::anyhow!("ModParams error: {:?}", e))?;

        let tx_params = self
            .lora
            .create_tx_packet_params(8, false, true, false, &mdltn_params)
            .map_err(|e| anyhow::anyhow!("TxParams error: {:?}", e))?;

        self.lora
            .enter_standby()
            .await
            .map_err(|e| anyhow::anyhow!("Standby error: {:?}", e))?;
        Ok(())
    }

    pub async fn transmit(&mut self, data: &[u8]) -> anyhow::Result<()> {
        let mdltn_params = self
            .lora
            .create_modulation_params(
                lora_phy::mod_params::SpreadingFactor::_9,
                lora_phy::mod_params::Bandwidth::_125KHz,
                lora_phy::mod_params::CodingRate::_4_7,
                915_000_000,
            )
            .map_err(|e| anyhow::anyhow!("ModParams error: {:?}", e))?;

        self.lora
            .prepare_for_tx(&mdltn_params, 14, false)
            .await
            .map_err(|e| anyhow::anyhow!("PrepareTx error: {:?}", e))?;

        self.lora
            .tx(
                &mdltn_params,
                &mut self
                    .lora
                    .create_tx_packet_params(8, false, true, false, &mdltn_params)
                    .unwrap(),
                data,
                0xFFFFFF,
            )
            .await
            .map_err(|e| anyhow::anyhow!("TX error: {:?}", e))?;

        Ok(())
    }
}
