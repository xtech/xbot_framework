// Package codegen generates Go service clients from a xbot service.json.
//
// It is the Go counterpart of the cog templates in codegen/templates: it reads
// the same schema, applies the same rules (normalized names, unique ids, type
// whitelist, array limits) and emits a client that composes the generic
// transport of this module (xbot.Conn) with the ids and typed calls of one
// service.
package codegen

import (
	"encoding/json"
	"fmt"
	"os"
	"regexp"
	"sort"
	"strings"
)

// Schema is a service.json.
type Schema struct {
	Type      string     `json:"type"`
	Version   int        `json:"version"`
	Enums     []Enum     `json:"enums"`
	Inputs    []Field    `json:"inputs"`
	Outputs   []Field    `json:"outputs"`
	Registers []Field    `json:"registers"`
	Functions []Function `json:"functions"`
}

// Enum is one entry of "enums". Bitmask enums store shifts instead of values.
type Enum struct {
	ID       string           `json:"id"`
	BaseType string           `json:"base_type"`
	Values   map[string]int64 `json:"values"`
	Bitmask  bool             `json:"bitmask"`
	ToString bool             `json:"to_string"`
}

// Field is an input, output or register.
type Field struct {
	ID       int    `json:"id"`
	Name     string `json:"name"`
	Type     string `json:"type"`
	Optional bool   `json:"optional"`
}

// Function is one RPC function.
type Function struct {
	ID         int         `json:"id"`
	Name       string      `json:"name"`
	ReturnType string      `json:"return_type"`
	Parameters []Parameter `json:"parameters"`
}

// Parameter is one RPC parameter.
type Parameter struct {
	ID   int    `json:"id"`
	Name string `json:"name"`
	Type string `json:"type"`
}

// rawTypes are the scalar types the wire format supports (C++: xbot_codegen.py).
var rawTypes = []string{
	"char", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
	"int8_t", "int16_t", "int32_t", "int64_t", "float", "double",
}

// maxArrayLength is the limit the framework applies to array returns.
const maxArrayLength = 1024

var arrayType = regexp.MustCompile(`^(.+)\[(\d+)\]$`)

// Load reads and validates a service.json.
func Load(path string) (*Schema, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	s := &Schema{}
	if err := json.Unmarshal(data, s); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	if err := s.validate(); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	return s, nil
}

// validate applies the same checks the C++ generator does, so a schema that
// builds for C++ also generates for Go (and a broken one fails loudly here).
func (s *Schema) validate() error {
	if s.Type == "" {
		return fmt.Errorf("service.json: missing \"type\"")
	}
	for _, f := range s.Functions {
		if len(f.Parameters) > 0 {
			if err := checkUniqueIDs("function "+f.Name, f.Parameters); err != nil {
				return err
			}
		}
		// Array returns are limited like in the C++ generator.
		if base, n, isArray := splitType(f.ReturnType); isArray && base != "char" {
			if n < 1 || n > maxArrayLength {
				return fmt.Errorf("function %s: return length %d must be 1..%d", f.Name, n, maxArrayLength)
			}
		}
	}
	if err := checkUniqueIDs("inputs", s.Inputs); err != nil {
		return err
	}
	if err := checkUniqueIDs("outputs", s.Outputs); err != nil {
		return err
	}
	if err := checkUniqueIDs("registers", s.Registers); err != nil {
		return err
	}

	types := map[string]bool{}
	for _, t := range rawTypes {
		types[t] = true
	}
	for _, e := range s.Enums {
		if len(e.Values) == 0 {
			// Same rule as the C++ generator: an enum without members cannot be
			// generated (and the to_string path would derive a count from nothing).
			return fmt.Errorf("enum %s: no values", e.ID)
		}
		if !types[e.BaseType] {
			return fmt.Errorf("enum %s: unknown base_type %q", e.ID, e.BaseType)
		}
		types[e.ID] = true
	}
	for _, set := range []struct {
		what   string
		fields []Field
		blob   bool
	}{
		{"input", s.Inputs, false}, {"output", s.Outputs, false}, {"register", s.Registers, true},
	} {
		for _, f := range set.fields {
			base, _, isArray := splitType(f.Type)
			if base == "blob" {
				if !set.blob {
					return fmt.Errorf("%s %s: \"blob\" is only allowed for registers", set.what, f.Name)
				}
				if isArray {
					return fmt.Errorf("%s %s: \"blob\" cannot be an array", set.what, f.Name)
				}
				continue
			}
			if !types[base] {
				return fmt.Errorf("%s %s: unknown type %q", set.what, f.Name, f.Type)
			}
		}
	}
	return nil
}

// checkUniqueIDs rejects duplicate ids and duplicate normalized names, which the
// generated Go methods would otherwise collide on.
func checkUniqueIDs(what string, things interface{}) error {
	seenIDs := map[int]bool{}
	seenNames := map[string]string{}
	switch v := things.(type) {
	case []Parameter:
		for _, p := range v {
			if err := addUnique(seenIDs, seenNames, what, p.ID, p.Name); err != nil {
				return err
			}
		}
	case []Field:
		for _, f := range v {
			if err := addUnique(seenIDs, seenNames, what, f.ID, f.Name); err != nil {
				return err
			}
		}
	}
	return nil
}

func addUnique(ids map[int]bool, names map[string]string, what string, id int, name string) error {
	if ids[id] {
		return fmt.Errorf("%s: duplicate id %d", what, id)
	}
	ids[id] = true
	normalized := normalizeName(name)
	if other, ok := names[normalized]; ok {
		return fmt.Errorf("%s: %q and %q normalize to the same Go name %q", what, other, name, normalized)
	}
	names[normalized] = name
	return nil
}

// normalizeName removes whitespace, exactly like xbot_codegen.toCamelCase: the
// generated method name for "File Exists" is "FileExists".
func normalizeName(name string) string {
	return strings.Join(strings.Fields(name), "")
}

// splitType splits "char[128]" into ("char", 128, true).
func splitType(t string) (base string, length int, isArray bool) {
	if m := arrayType.FindStringSubmatch(t); m != nil {
		var n int
		_, _ = fmt.Sscanf(m[2], "%d", &n)
		return m[1], n, true
	}
	return t, 0, false
}

// sortedEnumValues returns the enum values ordered by their numeric value, so
// the generated const block is deterministic.
func sortedEnumValues(e Enum) []string {
	keys := make([]string, 0, len(e.Values))
	for k := range e.Values {
		keys = append(keys, k)
	}
	sort.Slice(keys, func(i, j int) bool {
		if e.Values[keys[i]] != e.Values[keys[j]] {
			return e.Values[keys[i]] < e.Values[keys[j]]
		}
		return keys[i] < keys[j]
	})
	return keys
}
