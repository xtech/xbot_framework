package xbot

import (
	"errors"
	"fmt"
)

// Errors returned by this package. RPC failures are reported as an *RPCError,
// which matches ErrBusy or ErrRPC with errors.Is.
var (
	// ErrBusy means the service answered "busy": it is still executing another
	// call. Retrying later is reasonable.
	ErrBusy = errors.New("xbot: service busy")
	// ErrRPC means the service answered "error": the call itself failed (wrong
	// parameters, internal error). Retrying will not help.
	ErrRPC = errors.New("xbot: rpc failed")
	// ErrNotAdvertised means the service was not found on the discovery group.
	// The error message lists the service ids that *were* advertised.
	ErrNotAdvertised = errors.New("xbot: service not advertised")
)

// RPCError is returned by Conn.Call when the service answered the call with a
// status other than RPCStatusSuccess.
type RPCError struct {
	FunctionID uint8
	Status     uint8
}

// Error implements error.
func (e *RPCError) Error() string {
	reason := "error"
	if e.Status == RPCStatusBusy {
		reason = "busy"
	}
	return fmt.Sprintf("xbot: rpc function %d answered %s (status %d)", e.FunctionID, reason, e.Status)
}

// Is maps the status onto ErrBusy and ErrRPC, so callers do not have to look at
// the numeric status:
//
//	if errors.Is(err, xbot.ErrBusy) { retry() }
func (e *RPCError) Is(target error) bool {
	switch target {
	case ErrBusy:
		return e.Status == RPCStatusBusy
	case ErrRPC:
		return e.Status != RPCStatusBusy
	default:
		return false
	}
}
