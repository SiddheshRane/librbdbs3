#!/bin/bash

function checkDeps() {
    which go || { echo "Go is not installed. Install version 1.17"; exit 1; }
    which gcc || { echo "gcc is not installed."; exit 1; }
}

checkDeps

echo "Building bs3 static library"
cd bs3
go build -buildmode c-archive -o libbs3.a main.go
cd ..

echo "Building librbd.so app"
cd mylibrbd
#gcc librbd.c  -L../bs3/ -lbs3 -lpthread -o ../rbdtestapp   #Use this for the app version used for testing
gcc -shared -fPIC  librbd.c  -L../bs3/ -lbs3 -lpthread -o librbd.so
cd ..

echo executable is available rbdtestapp