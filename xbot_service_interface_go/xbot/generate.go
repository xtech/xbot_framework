package xbot

// The message types mirror `enum class MessageType` in the C++ header, so they are
// generated instead of maintained twice. CI runs this and commits the result, which is
// why adding a message type needs no Go toolchain on the developer's machine.
//
//go:generate go run ../cmd/xbot-codegen -message-types -header ../../include/xbot/datatypes/XbotHeader.hpp -out message_type_generated.go
