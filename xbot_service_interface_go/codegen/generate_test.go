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
		"blob array":                     `{"type": "X", "version": 1, "inputs": [{"id": 0, "name": "A", "type": "blob[4]"}]}`,
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
		"blob budget helpers collide": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "Write", "return_type": "void",
			 "parameters": [{"id": 0, "name": "PathData", "type": "blob"}]},
			{"id": 1, "name": "WritePath", "return_type": "void",
			 "parameters": [{"id": 0, "name": "Data", "type": "blob"}]}]}`,
		"string above one packet": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "void",
			 "parameters": [{"id": 0, "name": "P", "type": "string", "max_length": 1441}]}]}`,
		"array parameter above one packet": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "void",
			 "parameters": [{"id": 0, "name": "P", "type": "uint8_t[1441]"}]}]}`,
		// The rest mirrors the C++ generator, so a schema that cannot build there does not
		// produce a Go client either.
		"string array parameter": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "void",
			 "parameters": [{"id": 0, "name": "P", "type": "string[16]"}]}]}`,
		"blob array parameter": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "void",
			 "parameters": [{"id": 0, "name": "P", "type": "blob[16]"}]}]}`,
		"string array return": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "string[16]", "max_length": 16}]}`,
		"blob array return": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "blob[16]"}]}`,
		"enum array parameter above one packet": `{"type": "X", "version": 1,
			"enums": [{"id": "Mode", "base_type": "uint8_t", "values": {"A": 0}}],
			"functions": [{"id": 0, "name": "F", "return_type": "void",
			 "parameters": [{"id": 0, "name": "P", "type": "Mode[2000]"}]}]}`,
		"array return above one packet": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "uint32_t[512]"}]}`,
		"char array return above one packet": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "char[1449]"}]}`,
		"parameters above one packet together": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "void", "parameters": [
				{"id": 0, "name": "A", "type": "string", "max_length": 1000},
				{"id": 1, "name": "B", "type": "string", "max_length": 1000}]}]}`,
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

// TestBlobParameters pins that blob parameters and returns work: a blob parameter is
// sent as is (no padding to a fixed size) and a blob return is handed back as the raw
// payload.
func TestBlobParameters(t *testing.T) {
	path := t.TempDir() + "/service.json"
	schema := `{"type": "X", "version": 1, "functions": [
		{"id": 0, "name": "Echo", "return_type": "blob",
		 "parameters": [{"id": 0, "name": "Data", "type": "blob"}]},
		{"id": 1, "name": "Write", "return_type": "int32_t",
		 "parameters": [
			{"id": 0, "name": "Path", "type": "blob"},
			{"id": 1, "name": "Offset", "type": "uint32_t"},
			{"id": 2, "name": "Data", "type": "blob"},
			{"id": 3, "name": "Hash", "type": "uint32_t"}]}]}`
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
		"func (c *Client) Echo(ctx context.Context, Data []byte) ([]byte, error)",
		"func (c *Client) Write(ctx context.Context, Path []byte, Offset uint32, Data []byte, Hash uint32) (int32, error)",
		"xbot.Bytes(0, Data)",
		"xbot.Bytes(0, Path)",
		"return append([]byte(nil), payload...)",
		// One helper per blob parameter, with the framing of this call subtracted.
		// The budget saturates: a call whose other parameters use up the packet has
		// no room left for this one, and a negative int is not a budget.
		"func MaxEchoData() int {",
		"func MaxWriteData(Path []byte) int {",
		"return max(0, xbot.MaxPacketSize-xbot.HeaderSize-4*xbot.DescriptorSize-8-len(Path))",
	} {
		if !bytes.Contains(src, []byte(want)) {
			t.Errorf("generated code does not contain %q", want)
		}
	}
	// A blob must not be padded to a declared size - that was the whole point.
	for _, notWant := range []string{"byteParam(Data", "charParam(Path"} {
		if bytes.Contains(src, []byte(notWant)) {
			t.Errorf("generated code still pads a blob parameter: found %q", notWant)
		}
	}
}

// TestBlobHelperNames pins the identifier of the frame budget helper: it is built
// from the normalized RPC name, not from the raw one, or an RPC called "Write Data"
// would emit `func MaxWrite DataData()` and Generate would fail while formatting.
func TestBlobHelperNames(t *testing.T) {
	path := t.TempDir() + "/service.json"
	schema := `{"type": "X", "version": 1, "functions": [
		{"id": 0, "name": "Write Data", "return_type": "int32_t",
		 "parameters": [{"id": 0, "name": "Data", "type": "blob"}]}]}`
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
	if !bytes.Contains(src, []byte("func MaxWriteDataData() int {")) {
		t.Errorf("generated code does not contain the normalized helper name: %s", src)
	}
}

// path already carries a pointer and a length, only the schema rejected it.
func TestBlobInputOutput(t *testing.T) {
	path := t.TempDir() + "/service.json"
	schema := `{"type": "X", "version": 1,
		"inputs":  [{"id": 0, "name": "Data",  "type": "blob"}],
		"outputs": [{"id": 0, "name": "Extra", "type": "blob"}]}`
	if err := os.WriteFile(path, []byte(schema), 0o644); err != nil {
		t.Fatal(err)
	}
	s, err := Load(path)
	if err != nil {
		t.Fatalf("blob input/output rejected: %v", err)
	}
	if _, err := Generate(s, Options{}); err != nil {
		t.Fatalf("Generate: %v", err)
	}
}

// TestLoadAcceptsPacketLimits pins the other side of TestLoadRejects: the limits are
// inclusive, and a blob only counts with its descriptor because it may be empty.
func TestLoadAcceptsPacketLimits(t *testing.T) {
	cases := map[string]string{
		"array return of exactly one packet": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "uint32_t[362]"}]}`,
		"parameters that fill one packet together": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "void", "parameters": [
				{"id": 0, "name": "A", "type": "string", "max_length": 716},
				{"id": 1, "name": "B", "type": "string", "max_length": 716}]}]}`,
		"blobs next to a full size string": `{"type": "X", "version": 1, "functions": [
			{"id": 0, "name": "F", "return_type": "void", "parameters": [
				{"id": 0, "name": "A", "type": "string", "max_length": 1424},
				{"id": 1, "name": "B", "type": "blob"},
				{"id": 2, "name": "C", "type": "blob"}]}]}`,
		"enum array parameter": `{"type": "X", "version": 1,
			"enums": [{"id": "Mode", "base_type": "uint16_t", "values": {"A": 0}}],
			"functions": [{"id": 0, "name": "F", "return_type": "void",
			 "parameters": [{"id": 0, "name": "P", "type": "Mode[720]"}]}]}`,
	}
	for name, schema := range cases {
		t.Run(name, func(t *testing.T) {
			path := t.TempDir() + "/service.json"
			if err := os.WriteFile(path, []byte(schema), 0o644); err != nil {
				t.Fatal(err)
			}
			if _, err := Load(path); err != nil {
				t.Fatalf("Load: %v", err)
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

// TestStringParameters pins the generated string handling: a string parameter is a Go
// string, sent as its UTF-8 bytes without a terminator and refused above max_length; a
// string return is decoded into a string and checked against the limit.
func TestStringParameters(t *testing.T) {
	path := t.TempDir() + "/service.json"
	schema := `{"type": "X", "version": 1, "functions": [
		{"id": 0, "name": "Play", "return_type": "string", "max_length": 96,
		 "parameters": [{"id": 0, "name": "Path", "type": "string", "max_length": 64}]}]}`
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
		"func (c *Client) Play(ctx context.Context, Path string) (string, error) {",
		"if len(Path) > 64 { // string, max_length 64",
		"xbot.String(0, Path)",
		"if len(payload) > 96 {",
		"return string(payload), nil",
		// The budget helper treats a string like a blob: variable length.
		"func MaxPlayPath() int {",
		// The helper must not advertise more than the schema accepts, even though the
		// packet could carry more.
		"return min(64, max(0, xbot.MaxPacketSize-xbot.HeaderSize-",
	} {
		if !bytes.Contains(src, []byte(want)) {
			t.Errorf("generated code does not contain %q", want)
		}
	}
}
