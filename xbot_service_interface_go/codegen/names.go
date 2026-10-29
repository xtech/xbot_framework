package codegen

import (
	"strings"
	"unicode"
	"unicode/utf8"
)

// initialismNames lists the words Go writes in one case (ID, not Id; URL, not
// Url; Hz, not HZ), in their canonical spelling. The lookup map below is derived
// from it, keyed by the lower case spelling, so one entry covers CamelCase
// ("SoundId"), SCREAMING_SNAKE ("GPS_RTK_FIX") and prose ("State ID").
var initialismNames = []string{
	"ADC", "API", "BMS", "CAN", "CDC", "ESC", "GPS", "GPIO", "HTTP", "Hz",
	"I2C", "I2S", "ID", "IMU", "IP", "JSON", "LED", "MP3", "PCB", "PID",
	"PWM", "ROS", "RPC", "RTK", "SPI", "TCP", "UDP", "URL", "USB",
}

// initialisms maps the lower case spelling to the canonical one.
var initialisms = func() map[string]string {
	m := make(map[string]string, len(initialismNames))
	for _, name := range initialismNames {
		m[strings.ToLower(name)] = name
	}
	return m
}()

// goName converts a schema identifier into a Go identifier conforming to the Go
// naming rules for initialisms: "SoundId" -> "SoundID", "PlayMp3" -> "PlayMP3",
// "I2cReceive" -> "I2CReceive", "GPS_RTK_FIX" -> "GPSRTKFix", "IDLE" -> "Idle".
//
// Names that need no change come out unchanged, so the generator output stays
// close to the service.json (which keeps it greppable against the C++ side).
func goName(name string) string {
	words := splitWords(name)
	if len(words) == 0 {
		return ""
	}
	var b strings.Builder
	for _, w := range words {
		b.WriteString(goWord(w))
	}
	return b.String()
}

// splitWords splits on whitespace (spaces, tabs, newlines), underscores and
// dashes, and at CamelCase boundaries: "GPIOEvent" -> GPIO, Event; "I2cReceive" -> I2c, Receive;
// "Battery Voltage ADC" -> Battery, Voltage, ADC; "GPS_RTK_FIX" -> GPS, RTK, FIX.
func splitWords(name string) []string {
	isSep := func(r rune) bool { return r == '_' || r == '-' || unicode.IsSpace(r) }
	isUpper := func(r rune) bool { return r >= 'A' && r <= 'Z' }
	isLower := func(r rune) bool { return r >= 'a' && r <= 'z' }
	isDigit := func(r rune) bool { return r >= '0' && r <= '9' }

	runes := []rune(name)
	var words []string
	start := -1
	emit := func(end int) {
		if start >= 0 && end > start {
			words = append(words, string(runes[start:end]))
		}
		start = -1
	}

	for i := 0; i < len(runes); i++ {
		r := runes[i]
		if isSep(r) {
			emit(i)
			continue
		}
		if start < 0 {
			start = i
			continue
		}
		prev := runes[i-1]
		switch {
		case (isLower(prev) || isDigit(prev)) && isUpper(r):
			// "SoundId": a new word starts at the upper case letter.
			emit(i)
			start = i
		case isUpper(prev) && isUpper(r) && i+1 < len(runes) && isLower(runes[i+1]):
			// "GPIOEvent": the last upper case letter starts the next word.
			emit(i)
			start = i
		}
	}
	emit(len(runes))
	return words
}

// goWord renders one word: known initialisms as they are, an all upper case run
// of initialisms split into them (GPIOID -> GPIOID), everything else Title case
// (IDLE -> Idle, FIX -> Fix).
func goWord(word string) string {
	if mapped, ok := initialisms[strings.ToLower(word)]; ok {
		return mapped
	}
	if word == strings.ToUpper(word) {
		if parts, ok := splitInitialisms(word); ok {
			return parts
		}
	}
	// Title case, decoding the first rune so accented letters survive.
	r, size := utf8.DecodeRuneInString(word)
	if r == utf8.RuneError && size <= 1 {
		return word
	}
	return string(unicode.ToUpper(r)) + strings.ToLower(word[size:])
}

// splitInitialisms splits an all upper case word greedily into initialisms
// ("GPIOID" -> "GPIO"+"ID"). It reports false unless the whole word is covered,
// so a normal word in capitals (IDLE) is not mangled into "ID"+"LE".
func splitInitialisms(word string) (string, bool) {
	lower := strings.ToLower(word)
	var b strings.Builder
	parts := 0
	for pos := 0; pos < len(lower); {
		best := ""
		for candidate := range initialisms {
			if len(candidate) > len(best) && strings.HasPrefix(lower[pos:], candidate) {
				best = candidate
			}
		}
		if best == "" {
			return "", false
		}
		b.WriteString(initialisms[best])
		pos += len(best)
		parts++
	}
	// A word that is a single initialism is already handled by the table lookup
	// in goWord, so only report a real split.
	if parts < 2 {
		return "", false
	}
	return b.String(), true
}
