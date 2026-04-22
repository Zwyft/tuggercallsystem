# Dev Optimized E-ink Firmware Branch

This branch contains firmware optimized specifically for the Heltec Vision Master E290 e-ink display.

## Key Optimizations

### Display Enhancements
- **Smart Refresh Strategy**: Intelligent full/partial refresh system to minimize flicker
- **Content-Aware Updates**: Only refresh when content actually changes
- **Optimized Typography**: Enhanced text clarity and spacing for 296x128 display
- **Improved Visual Hierarchy**: Better contrast and layout for easy reading

### Performance Improvements
- **Partial Refresh**: Faster updates for minor changes (< 500ms vs 2-3s full refresh)
- **Full Refresh**: Only every 30 seconds or after major state changes
- **Memory Efficient**: Content hashing to detect changes without full scanning
- **Reduced Flicker**: Optimized timing for E290 panel characteristics

### Device-Specific Optimizations

#### Line Device
- Shows actual order time instead of "Sent"
- Instant visual feedback when orders are made/cleared
- Optimized menu selection interface

#### Tugger Device  
- Improved order list display with better contrast
- Dual-zone support with clear visual indicators
- Enhanced peer status display

#### Collector Device
- Optimized order management interface
- Better WiFi status integration
- Improved timeout and claim visual feedback

### Technical Details

- **E-ink Controller**: DEPG0290BNS800 specific optimizations
- **Refresh Strategy**: Full refresh every 30s, partial refresh for minor updates
- **Text Rendering**: setFactor(0.75f) for optimal clarity
- **Color Scheme**: Black text on white background for best readability
- **Response Time**: < 100ms display updates for user interactions

## Files Modified

- `firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino`
- `firmware/LineDeviceFirmware/LineDeviceFirmware.ino`  
- `firmware/TuggerDeviceFirmware/TuggerFirmware.ino`

## Usage

1. Flash the appropriate firmware to your Heltec Vision Master E290 device
2. The display will automatically optimize refresh patterns based on content
3. Orders will appear instantly with proper timing and visual feedback

## Branch Information

- **Branch Name**: dev-optimized-eink
- **Base**: main
- **Target**: Production deployment after testing
