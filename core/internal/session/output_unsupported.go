//go:build !darwin

package session

import (
	"context"
	"errors"
)

type nativeOutputIO struct{}

func (nativeOutputIO) Read(int, []byte) (int, error) {
	return 0, errors.New("unsupported output platform")
}
func (nativeOutputIO) Wait(context.Context, int) error {
	return errors.New("unsupported output platform")
}
