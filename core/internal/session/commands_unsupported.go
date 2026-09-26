//go:build !darwin

package session

import (
	"context"
	"errors"
)

type nativeCommandIO struct{}

func (nativeCommandIO) Write(int, []byte) (int, error) {
	return 0, errors.New("agentvision-core: unsupported platform (Darwin required)")
}
func (nativeCommandIO) Resize(int, uint16, uint16) error {
	return errors.New("agentvision-core: unsupported platform (Darwin required)")
}
func (nativeCommandIO) Wait(context.Context, int) error {
	return errors.New("agentvision-core: unsupported platform (Darwin required)")
}
