package codegen

import "testing"

// TestGoName pins the naming rules against the identifiers that actually occur in
// the OpenMower service definitions plus the tricky shapes (all caps runs,
// digits, prose with spaces).
func TestGoName(t *testing.T) {
	cases := map[string]string{
		// initialisms in CamelCase
		"SoundId":           "SoundID",
		"PlayMp3":           "PlayMP3",
		"BusID":             "BusID",
		"GPIOID":            "GPIOID",
		"GpsQuality":        "GPSQuality",
		"I2cReceive":        "I2CReceive",
		"I2cTransmit":       "I2CTransmit",
		"I2cResult":         "I2CResult",
		"BatteryVoltageADC": "BatteryVoltageADC",
		"ReadGPIO":          "ReadGPIO",
		"SubscribeGPIO":     "SubscribeGPIO",
		"GPIOEventFlags":    "GPIOEventFlags",
		// constant style: SCREAMING_SNAKE
		"GPS_RTK_FIX":     "GPSRTKFix",
		"ROS_CONNECTED":   "ROSConnected",
		"RosDisconnected": "ROSDisconnected",
		"EscStatus":       "ESCStatus",
		"GPS_RTK_LOST":    "GPSRTKLost",
		"BOOT_PING":       "BootPing",
		"IDLE":            "Idle",
		"MP3":             "MP3",
		"TONE":            "Tone",
		// prose names from the registers
		"State ID":            "StateID",
		"Gps Quality":         "GPSQuality",
		"GPIO Configs":        "GPIOConfigs",
		"Battery Voltage ADC": "BatteryVoltageADC",
		"Charger Enabled":     "ChargerEnabled",
		"Sound Definitions":   "SoundDefinitions",
		"Simulated Inputs":    "SimulatedInputs",
		// untouched names
		"Path":       "Path",
		"Hash":       "Hash",
		"DurationMs": "DurationMs",
		"DetuneHz":   "DetuneHz",
		"FileExists": "FileExists",
		"Waveform":   "Waveform",
	}
	for in, want := range cases {
		if got := goName(in); got != want {
			t.Errorf("goName(%q) = %q, want %q", in, got, want)
		}
	}
}
