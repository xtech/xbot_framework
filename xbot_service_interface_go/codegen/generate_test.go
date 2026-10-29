package codegen

import (
	"bytes"
	"flag"
	"os"
	"testing"
)

var update = flag.Bool("update", false, "rewrite the golden files")

// TestGenerateGolden pins the generated output for the demo schema (the same
// service.json the C++ templates use). Run with -update after an intentional
// change and review the diff.
func TestGenerateGolden(t *testing.T) {
	cases := []struct {
		name    string
		schema  string
		golden  string
		options Options
	}{
		{"demo", "testdata/demo_service.json", "testdata/demo_service.golden.go",
			Options{ServiceID: 99}},
		{"demo without service id", "testdata/demo_service.json",
			"testdata/demo_service_noid.golden.go", Options{}},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			schema, err := Load(c.schema)
			if err != nil {
				t.Fatalf("Load: %v", err)
			}
			got, err := Generate(schema, c.options)
			if err != nil {
				t.Fatalf("Generate: %v", err)
			}
			if *update {
				if err := os.WriteFile(c.golden, got, 0o644); err != nil {
					t.Fatalf("write golden: %v", err)
				}
				return
			}
			want, err := os.ReadFile(c.golden)
			if err != nil {
				t.Fatalf("read golden (run with -update): %v", err)
			}
			if !bytes.Equal(got, want) {
				t.Errorf("generated output differs from %s (run with -update and review the diff)", c.golden)
			}
		})
	}
}

// TestLoadRejects makes sure a schema the C++ generator would reject fails here
// as well, instead of producing a client that cannot work.
func TestLoadRejects(t *testing.T) {
	cases := map[string]string{
		"missing type":                   `{"version": 1}`,
		"unknown type":                   `{"type": "X", "version": 1, "inputs": [{"id": 0, "name": "A", "type": "uint7_t"}]}`,
		"blob as input":                  `{"type": "X", "version": 1, "inputs": [{"id": 0, "name": "A", "type": "blob"}]}`,
		"duplicate ids":                  `{"type": "X", "version": 1, "inputs": [{"id": 1, "name": "A", "type": "uint8_t"}, {"id": 1, "name": "B", "type": "uint8_t"}]}`,
		"name collision":                 `{"type": "X", "version": 1, "inputs": [{"id": 0, "name": "My Input", "type": "uint8_t"}, {"id": 1, "name": "MyInput", "type": "uint8_t"}]}`,
		"unknown enum base":              `{"type": "X", "version": 1, "enums": [{"id": "E", "base_type": "uint7_t", "values": {"A": 0}}]}`,
		"empty enum":                     `{"type": "X", "version": 1, "enums": [{"id": "E", "base_type": "uint8_t", "to_string": true, "values": {}}]}`,
		"function shadows Client method": `{"type": "X", "version": 1, "functions": [{"id": 0, "name": "Close", "return_type": "void"}]}`,
		"function and register share a setter": `{"type": "X", "version": 1,
			"functions": [{"id": 0, "name": "SetVolume", "return_type": "void"}],
			"registers": [{"id": 0, "name": "Volume", "type": "uint8_t"}]}`,
		"functions normalize equal": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "SoundId", "return_type": "void"},
			{"id": 1, "name": "SoundID", "return_type": "void"}]}`,
		"enum values normalize equal": `{"type": "X", "version": 1, "enums": [{"id": "E", "base_type": "uint8_t",
			"values": {"IDLE": 0, "Idle": 1}}]}`,
		"enum types normalize equal": `{"type": "X", "version": 1, "enums": [
			{"id": "SoundId", "base_type": "uint8_t", "values": {"A": 0}},
			{"id": "SoundID", "base_type": "uint8_t", "values": {"A": 0}}]}`,
	}
	for name, schema := range cases {
		t.Run(name, func(t *testing.T) {
			path := t.TempDir() + "/service.json"
			if err := os.WriteFile(path, []byte(schema), 0o644); err != nil {
				t.Fatal(err)
			}
			if _, err := Load(path); err == nil {
				t.Fatal("expected an error")
			}
		})
	}
}

// TestNumericArrays pins that numeric arrays (T[n]) are supported: they are
// padded to n elements on send and decoded from n elements on receive.
func TestNumericArrays(t *testing.T) {
	path := t.TempDir() + "/service.json"
	schema := `{"type": "X", "version": 1, "functions": [{"id": 0, "name": "F", "return_type": "double[3]",
"parameters": [{"id": 0, "name": "P", "type": "uint16_t[4]"}]}]}`
	if err := os.WriteFile(path, []byte(schema), 0o644); err != nil {
		t.Fatal(err)
	}
	s, err := Load(path)
	if err != nil {
		t.Fatalf("Load: %v", err)
	}
	src, err := Generate(s, Options{})
	if err != nil {
		t.Fatalf("Generate: %v", err)
	}
	for _, want := range []string{
		"func (c *Client) F(ctx context.Context, P []uint16) ([]float64, error)",
		"xbot.Bytes(0, leUint16Slice(P, 4))",
		"return readFloat64Slice(payload, 3)",
		"func padFloat64Slice(",
	} {
		if !bytes.Contains(src, []byte(want)) {
			t.Errorf("generated code does not contain %q", want)
		}
	}
}
