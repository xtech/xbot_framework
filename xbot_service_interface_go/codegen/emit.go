package codegen

import (
	"fmt"
	"go/format"
	"strings"
)

// Options configure Generate.
type Options struct {
	// ServiceID is written as a `ServiceID` constant and used by Dial. 0 emits
	// no constant: Dial then takes the id as its first argument (like the C++
	// generated interface, which gets the id at construction time).
	ServiceID uint16
	// Package overrides the generated package name (default: the schema type in
	// lower case, e.g. "FileService" -> "fileservice").
	Package string
}

// Generate renders a Go client for s.
func Generate(s *Schema, opts Options) ([]byte, error) {
	g := &generator{schema: s, opts: opts, needs: map[string]bool{}}
	if g.opts.Package == "" {
		g.opts.Package = packageName(s.Type)
	}
	if err := g.run(); err != nil {
		return nil, err
	}
	return format.Source([]byte(g.body.String()))
}

var goTypes = map[string]string{
	"uint8_t": "uint8", "uint16_t": "uint16", "uint32_t": "uint32", "uint64_t": "uint64",
	"int8_t": "int8", "int16_t": "int16", "int32_t": "int32", "int64_t": "int64",
	"float": "float32", "double": "float64",
}

var typeSizes = map[string]int{
	"uint8_t": 1, "int8_t": 1,
	"uint16_t": 2, "int16_t": 2,
	"uint32_t": 4, "int32_t": 4, "float": 4,
	"uint64_t": 8, "int64_t": 8, "double": 8,
}

type generator struct {
	schema *Schema
	opts   Options
	body   strings.Builder
	// needs collects the numeric array helpers the generated file refers to, so
	// only those are emitted (the scalar helpers are always emitted).
	needs map[string]bool
}

// line appends a line verbatim.
func (g *generator) line(s string) { g.body.WriteString(s); g.body.WriteByte('\n') }

// f appends a formatted line.
func (g *generator) f(format string, args ...any) {
	fmt.Fprintf(&g.body, format, args...)
	g.body.WriteByte('\n')
}

// packageName turns "FileService" into "fileservice".
func packageName(serviceType string) string {
	var b strings.Builder
	for _, r := range strings.ToLower(serviceType) {
		if (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9') {
			b.WriteRune(r)
		}
	}
	return b.String()
}

// enumBase returns the base type of an enum ("" if t is not an enum).
func (g *generator) enumBase(t string) string {
	for _, e := range g.schema.Enums {
		if e.ID == t {
			return e.BaseType
		}
	}
	return ""
}

// goScalar maps a scalar schema type to its Go type, resolving enums.
func (g *generator) goScalar(t, where string) (string, error) {
	if gt, ok := goTypes[t]; ok {
		return gt, nil
	}
	if g.enumBase(t) != "" {
		return goName(t), nil
	}
	return "", fmt.Errorf("go codegen: unsupported type %q in %s", t, where)
}

// goType maps a schema type (with optional array suffix) to a Go type.
func (g *generator) goType(t, where string) (string, error) {
	base, _, isArray := splitType(t)
	switch {
	case base == "blob":
		return "[]byte", nil
	case base == "char":
		return "string", nil // char[n] is a padded string on the wire
	case isArray && base == "uint8_t":
		return "[]byte", nil
	case isArray:
		gt, err := g.goScalar(base, where)
		if err != nil {
			return "", err
		}
		return "[]" + gt, nil
	}
	return g.goScalar(base, where)
}

// packExpr turns a Go value into the bytes of one RPC parameter.
func (g *generator) packExpr(t, expr, where string) (string, error) {
	base, length, isArray := splitType(t)
	switch {
	case base == "blob":
		return expr, nil
	case base == "char":
		return fmt.Sprintf("charParam(%s, %d)", expr, length), nil
	case isArray && base == "uint8_t":
		return fmt.Sprintf("byteParam(%s, %d)", expr, length), nil
	case isArray:
		kind, err := g.sliceKind(base, where)
		if err != nil {
			return "", err
		}
		g.needs[kind] = true
		return fmt.Sprintf("le%sSlice(%s, %d)", kind, expr, length), nil
	}
	if eb := g.enumBase(base); eb != "" {
		gt := goTypes[eb]
		return fmt.Sprintf("le%s(%s(%s))", upperFirst(gt), gt, expr), nil
	}
	gt, err := g.goScalar(base, where)
	if err != nil {
		return "", err
	}
	return fmt.Sprintf("le%s(%s)", upperFirst(gt), expr), nil
}

// decodeReturn returns the Go return type ("" for void) and the lines that decode
// the RPC response payload.
func (g *generator) decodeReturn(t, fn string) (string, []string, error) {
	base, _, isArray := splitType(t)
	switch {
	case t == "" || t == "void":
		return "", nil, nil
	case base == "char" && isArray:
		return "string", []string{"\treturn charValue(payload), nil"}, nil
	case isArray && base == "uint8_t":
		return "[]byte", []string{"\treturn append([]byte(nil), payload...), nil"}, nil
	case isArray:
		kind, err := g.sliceKind(base, "return value of "+fn)
		if err != nil {
			return "", nil, err
		}
		g.needs[kind] = true
		gt, err := g.goScalar(base, "return value of "+fn)
		if err != nil {
			return "", nil, err
		}
		_, length, _ := splitType(t)
		return "[]" + gt, []string{fmt.Sprintf("\treturn read%sSlice(payload, %d)", kind, length)}, nil
	}
	goType, err := g.goScalar(base, "return value of "+fn)
	if err != nil {
		return "", nil, err
	}
	wireType := base
	if eb := g.enumBase(base); eb != "" {
		wireType = eb
	}
	size := typeSizes[wireType]
	read := fmt.Sprintf("binary.LittleEndian.Uint%d(payload[:%d])", size*8, size)
	lines := []string{
		fmt.Sprintf("\tif len(payload) < %d {", size),
		fmt.Sprintf("\t\treturn ZERO, errf(%q, len(payload))",
			fmt.Sprintf("%s returned %%d bytes, want %d", fn, size)),
		"\t}",
	}
	switch {
	case wireType == "uint8_t":
		lines = append(lines, "\treturn payload[0], nil")
	case strings.HasPrefix(wireType, "int"):
		lines = append(lines, fmt.Sprintf("\treturn int%d(%s), nil", size*8, read))
	case strings.HasPrefix(wireType, "float") || wireType == "double":
		lines = append(lines, fmt.Sprintf("\treturn math.Float%dfrombits(%s), nil", size*8, read))
	default:
		lines = append(lines, fmt.Sprintf("\treturn %s, nil", read))
	}
	return goType, lines, nil
}

// zeroValue is the Go zero value of a return type, used in the error path.
// zeroValue is the Go zero value of a generated return type: "" for strings, nil
// for slices ([]byte like the uint8_t arrays, and the numeric array types such as
// []float64) and 0 for everything else (numeric scalars and enums).
func zeroValue(goType string) string {
	switch {
	case goType == "string":
		return `""`
	case strings.HasPrefix(goType, "[]"):
		return "nil"
	}
	return "0"
}

// upperFirst capitalises the first letter ("uint8" -> "Uint8").
func upperFirst(s string) string {
	if s == "" {
		return s
	}
	return strings.ToUpper(s[:1]) + s[1:]
}

// sliceKinds maps a numeric base type to the name of its array helper.
var sliceKinds = map[string]string{
	"uint16_t": "Uint16", "uint32_t": "Uint32", "uint64_t": "Uint64",
	"int8_t": "Int8", "int16_t": "Int16", "int32_t": "Int32", "int64_t": "Int64",
	"float": "Float32", "double": "Float64",
}

// sliceKind returns the array helper for an element type (enums resolve to their
// base type).
func (g *generator) sliceKind(base, where string) (string, error) {
	if eb := g.enumBase(base); eb != "" {
		base = eb
	}
	if kind, ok := sliceKinds[base]; ok {
		return kind, nil
	}
	return "", fmt.Errorf("go codegen: unsupported array element type %q in %s", base, where)
}
