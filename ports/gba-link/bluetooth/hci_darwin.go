package main

import (
	"context"
	"errors"
)

func intervalMonitor(_, _ uint16) (func(context.Context) error, func(), error) {
	return nil, nil, errors.New("HCI interval monitoring is only supported on Brick Linux")
}

func controllerCommand(_ uint16, _ []byte) error {
	return errors.New("legacy HCI commands are only supported on Brick Linux")
}
