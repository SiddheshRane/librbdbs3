// Copyright (C) 2021 Vojtech Aschenbrenner <v@asch.cz>

// bs3 is a userspace daemon using BUSE for creating a block device and S3
// protocol to communicate with object backend. It is designed for easy
// extension of all the important parts. Hence the S3 protocol can be easily
// replaced by RADOS or any other protocol.
//
// Project structure is following:
//
// - internal contains all packages used by this program. The name "internal"
// is reserved by go compiler and disallows its imports from different
// projects. Since we don't provide any reusable packages, we use internal
// directory.
//
// - internal/bs3 contains all packages related only to the bs3 implementation.
// See the package descriptions in the source code for more details.
//
// - internal/null contains trivial implementation of block device which does
// nothing but correctly. It can be used for benchmarking underlying buse
// library and kernel module. The null implementation is part of bs3 because it
// shares configuration and makes benchmarking easier and without code
// duplication.
//
// - internal/config contains configuration package which is common for both,
// bs3 and null implementations.
package main

//#include "../mylibrbd/librbd.h"
//extern void go_dummy_callback(AioCompletion* c);
//extern void go_aio_read_complete(AioCompletion*);
//#cgo LDFLAGS: -Wl,-unresolved-symbols=ignore-in-object-files
import "C"

import (
	"fmt"
	"os"
	"time"
	"unsafe"

	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"

	"github.com/asch/bs3/internal/bs3"
	"github.com/asch/bs3/internal/config"
	"github.com/asch/bs3/internal/null"
	"github.com/asch/buse/lib/go/buse"
)

// Parse configuration from file and environment variables, creates a
// BuseReadWriter and creates new buse device with it. The device is ran until
// it is signaled by SIGINT or SIGTERM to gracefully finish.
func main() {
	loggerSetup(config.Cfg.Log.Pretty, config.Cfg.Log.Level)
}

// Return null device if user wants it, otherwise returns bs3 device, which is
// default.
func getBuseReadWriter(wantNullDevice bool) (buse.BuseReadWriter, error) {
	if wantNullDevice {
		return null.NewNull(), nil
	}

	bs3, err := bs3.NewWithDefaults()

	return bs3, err
}

func loggerSetup(pretty bool, level int) {
	if pretty {
		log.Logger = log.Output(zerolog.ConsoleWriter{Out: os.Stderr})
	}

	zerolog.SetGlobalLevel(zerolog.Level(level))
}

var buseReadWriter buse.BuseReadWriter

//export bs3Open
func bs3Open(name string) int {
	//read config
	config.Configure()
	//use name as bucket
	config.Cfg.S3.Bucket = name

	var err error
	buseReadWriter, err = bs3.NewWithDefaults()
	if err != nil {
		return -1
	}

	buseReadWriter.BusePreRun()

	return 0
}

//export bs3Close
func bs3Close(name string) int {
	//name is irrelevant here because right now we only allow 1 instance
	if buseReadWriter != nil {
		buseReadWriter.BusePostRemove()
		buseReadWriter = nil
	}
	return 0
}

//export bs3Flush
func bs3Flush(name string) {
	//we should expose bs3.checkpoint as that seems to be doing what this operation requests
}

//export bs3Stat
func bs3Stat() (disk_size, block_size uint64) {
	config.Configure() //for testing
	disk_size = uint64(config.Cfg.Size)
	block_size = uint64(config.Cfg.BlockSize)
	return
}

//export bs3Read
func bs3Read(offset, length int64, buffer []byte, completion *C.AioCompletion) {
	//convert offset to sector
	sector := offset / int64(config.Cfg.BlockSize)
	buseReadWriter.BuseRead(sector, length, buffer)
	C.go_aio_read_complete(completion)
}

/*
 * Functions for testing the C-Go interface
 */

//export bs3ReadTest
func bs3ReadTest(offset, length int64, buf unsafe.Pointer, completion uintptr) {
	fmt.Println("offset:", offset, "length:", length, "buf ptr", buf)
	sl := unsafe.Slice((*byte)(buf), length)
	sl[0] = '$'
}

//export bs3WriteTest
func bs3WriteTest(offset, length int64, buffer []byte) {
	for i, _ := range buffer {
		buffer[i] = 'a'
	}
	buffer = append(buffer, 'b', 'c')
}

//export bs3Async
func bs3Async(completion *C.AioCompletion) {
	go func() {
		time.Sleep(time.Duration(1) * time.Second)
		completion.return_value = 123
	}()
}

//export bs3CallbackTest
func bs3CallbackTest(completion *C.AioCompletion) {
	go func() {
		time.Sleep(time.Duration(1) * time.Second)
		C.go_dummy_callback(completion)
	}()
}
