package codegen

import "fmt"

// fixedPackageNames are the package level identifiers every generated file has;
// a schema name must not shadow them. ServiceID is reserved even when no
// -service-id is passed, so a schema stays valid for both invocations.
var fixedPackageNames = []string{
	"Client", "Dial", "ServiceID", "errf",
	"leUint8", "leUint16", "leUint32", "leUint64",
	"leInt8", "leInt16", "leInt32", "leInt64", "leFloat32", "leFloat64",
	"byteParam", "charParam", "charValue",
}

// fixedClientMethods are the methods every generated Client has.
var fixedClientMethods = []string{"Close", "Endpoint"}

// validateGeneratedNames rejects schemas whose generated Go identifiers would
// collide, before any code is emitted. Two namespaces matter:
//
//   - package level: the Fn* function-id constants, the enum types and their
//     values, and the generated helpers;
//   - the Client: one method per function, register setter and input sender,
//     plus the fixed Close and Endpoint.
//
// The comparison uses the same goName rules as the emitters, so a collision that
// only appears after the translation (IDLE and Idle, SoundId and SoundID) is
// caught as well.
func (s *Schema) validateGeneratedNames() error {
	pkg := make(map[string]string, len(fixedPackageNames)+len(s.Functions))
	for _, name := range fixedPackageNames {
		pkg[name] = "the generator"
	}
	method := make(map[string]string, len(fixedClientMethods))
	for _, name := range fixedClientMethods {
		method[name] = "the generator"
	}

	reserve := func(m map[string]string, name, what string) error {
		if prev, ok := m[name]; ok {
			return fmt.Errorf("%s and %s both generate the Go name %q", prev, what, name)
		}
		m[name] = what
		return nil
	}

	for _, fn := range s.Functions {
		name := goName(fn.Name)
		if err := reserve(pkg, "Fn"+name, "function "+fn.Name); err != nil {
			return err
		}
		if err := reserve(method, name, "function "+fn.Name); err != nil {
			return err
		}
	}
	for _, r := range s.Registers {
		if err := reserve(method, "Set"+goName(r.Name), "register "+r.Name); err != nil {
			return err
		}
	}
	for _, in := range s.Inputs {
		if err := reserve(method, "Send"+goName(in.Name), "input "+in.Name); err != nil {
			return err
		}
	}
	for _, e := range s.Enums {
		typeName := goName(e.ID)
		if err := reserve(pkg, typeName, "enum "+e.ID); err != nil {
			return err
		}
		for _, value := range sortedEnumValues(e) {
			if err := reserve(pkg, typeName+goName(value), "value "+value+" of enum "+e.ID); err != nil {
				return err
			}
		}
	}
	return nil
}
